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
#include "onvif_pkt.h"
#include "sha1.h"
#include "onvif_utils.h"
#include "onvif.h"
#include "onvif_req.h"
#include "base64.h"
#include "onvif_api.h"


/***************************************************************************************/
static const char xml_hdr[] = 
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

static const char onvif_xmlns[] = 
    "<s:Envelope "
    "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" " 
    "xmlns:enc=\"http://www.w3.org/2003/05/soap-encoding\" " 
    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" " 
    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" " 
    "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" "    
    "xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\" " 
    "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\" " 
    "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" " 
    "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\" " 
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" " 
    "xmlns:tr2=\"http://www.onvif.org/ver20/media/wsdl\" " 
    "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" "
    "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
    "xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\" "
    "xmlns:tan=\"http://www.onvif.org/ver20/analytics/wsdl\" "
    "xmlns:pt=\"http://www.onvif.org/ver10/pacs\" "
#ifdef DEVICEIO_SUPPORT
    "xmlns:tmd=\"http://www.onvif.org/ver10/deviceIO/wsdl\" "
#endif
#ifdef PROFILE_G_SUPPORT    
    "xmlns:trp=\"http://www.onvif.org/ver10/replay/wsdl\" "
    "xmlns:tse=\"http://www.onvif.org/ver10/search/wsdl\" "
    "xmlns:trc=\"http://www.onvif.org/ver10/recording/wsdl\" "
#endif    
#ifdef PROFILE_C_SUPPORT
    "xmlns:tac=\"http://www.onvif.org/ver10/accesscontrol/wsdl\" "
    "xmlns:tdc=\"http://www.onvif.org/ver10/doorcontrol/wsdl\" "
#endif
#ifdef THERMAL_SUPPORT
    "xmlns:tth=\"http://www.onvif.org/ver10/thermal/wsdl\" "
#endif
#ifdef CREDENTIAL_SUPPORT
    "xmlns:tcr=\"http://www.onvif.org/ver10/credential/wsdl\" "
#endif
#ifdef ACCESS_RULES
    "xmlns:tar=\"http://www.onvif.org/ver10/accessrules/wsdl\" "
#endif
#ifdef SCHEDULE_SUPPORT
    "xmlns:tsc=\"http://www.onvif.org/ver10/schedule/wsdl\" "
#endif
#ifdef RECEIVER_SUPPORT
    "xmlns:trv=\"http://www.onvif.org/ver10/receiver/wsdl\" "
#endif
#ifdef PROVISIONING_SUPPORT
    "xmlns:tpv=\"http://www.onvif.org/ver10/provisioning/wsdl\" "
#endif
    "xmlns:ter=\"http://www.onvif.org/ver10/error\" >";

static const char soap_body[] = 
    "<s:Body>";

static const char soap_tailer[] =
    "</s:Body></s:Envelope>";

    
/***************************************************************************************/
#define NONCELEN    20
#define SHA1_SIZE   20

void onvif_calc_nonce(char nonce[NONCELEN])
{ 
    static int count = 0xCA53;
      
    snprintf(nonce, NONCELEN, "%06x%04x%06x", (int)time(NULL) & 0xFFFFFF, count++, (int)rand() & 0xFFFFFF);
}

void onvif_calc_digest(const char *created, const char *nonce, int noncelen, const char *password, char hash[SHA1_SIZE])
{
    sha1_context ctx;
    
    sha1_starts(&ctx);
    sha1_update(&ctx, (uint8 *)nonce, noncelen);
    sha1_update(&ctx, (uint8 *)created, (int)strlen(created));
    sha1_update(&ctx, (uint8 *)password, (int)strlen(password));
    sha1_finish(&ctx, (uint8 *)hash);
}

void onvif_get_created_time(ONVIF_DEVICE * p_dev, char * created, int size)
{
    if (p_dev->timeType == TIME_TYPE_UTC)
    {            
        onvif_DateTime dt;
        memcpy(&dt, &p_dev->devTime, sizeof(onvif_DateTime));

        dt.Time.Second += (int)(time(NULL) - p_dev->fetchTime);
        dt.Time.Minute += dt.Time.Second / 60;
        dt.Time.Hour += dt.Time.Minute / 60;
        dt.Time.Minute %= 60;
        dt.Time.Second %= 60;

        if (dt.Time.Hour >= 24)
        {
            p_dev->needAuth = FALSE;
            p_dev->timeType = TIME_TYPE_INVALID;
            
            GetSystemDateAndTime(p_dev);

            memcpy(&dt, &p_dev->devTime, sizeof(onvif_DateTime));
        }
                    
        snprintf(created, size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            dt.Date.Year, dt.Date.Month, dt.Date.Day,
            dt.Time.Hour, dt.Time.Minute, dt.Time.Second);
    }
    else
    {
        onvif_format_datetime_str(time(NULL), 1, "%Y-%m-%dT%H:%M:%SZ", created, size);
    }
}

int build_onvif_req_header(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, const char * to, eOnvifAction type)
{
    int offset = 0;

    if (etdsGetSystemDateAndTime == type && p_dev->fetchTime == 0)
    {
        // The first GetSystemDateAndTime request does not require a password
        p_dev->fetchTime = time(NULL);
    }
    else if (p_dev->needAuth && p_dev->username[0] != '\0')  // maybe the password is NULL
    {
        char HA[SHA1_SIZE] = {'\0'}, HABase64[100] = {'\0'};
        char nonce[NONCELEN] = {'\0'}, nonceBase64[100] = {'\0'};
        char created[64] = {'\0'};

        onvif_get_created_time(p_dev, created, sizeof(created));
    
        onvif_calc_nonce(nonce);

        base64_encode((uint8*)nonce, (uint32)strlen(nonce), nonceBase64, sizeof(nonceBase64));

        onvif_calc_digest(created, nonce, (int)strlen(nonce), p_dev->password, HA);

        base64_encode((uint8*)HA, SHA1_SIZE, HABase64, sizeof(HABase64));

        offset += snprintf(p_buf+offset, mlen-offset, "<s:Header>");

        if (to != NULL)
        {
            offset += snprintf(p_buf+offset, mlen-offset, "<wsa:To>%s</wsa:To>", to);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsse:Security>"
            "<wsse:UsernameToken>"
            "<wsse:Username>%s</wsse:Username>"
            "<wsse:Password "
            "Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
            "%s</wsse:Password>"
            "<wsse:Nonce "
            "EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
            "%s</wsse:Nonce>"
            "<wsu:Created>%s</wsu:Created>"
            "</wsse:UsernameToken>"
            "</wsse:Security>", 
            p_dev->username, 
            HABase64, 
            nonceBase64, 
            created);

        offset += snprintf(p_buf+offset, mlen-offset, "</s:Header>");
    }
    else if (to != NULL)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<s:Header>"
                "<wsa:To>%s</wsa:To>"
            "</s:Header>",
            to);
    }

    return offset;
}

int build_onvif_req_header_ex(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, const char * to, const char * action, const char * parameters)
{
    int offset = 0;
    char uuid[100] = {'\0'};
    
    if (p_dev->needAuth && p_dev->username[0] != '\0')  // maybe the password is NULL
    {
        char HA[SHA1_SIZE] = {'\0'}, HABase64[100] = {'\0'};
        char nonce[NONCELEN] = {'\0'}, nonceBase64[100] = {'\0'};
        char created[64] = {'\0'};

        onvif_get_created_time(p_dev, created, sizeof(created));

        onvif_calc_nonce(nonce);

        base64_encode((uint8*)nonce, (uint32)strlen(nonce), nonceBase64, sizeof(nonceBase64));

        onvif_calc_digest(created, nonce, (int)strlen(nonce), p_dev->password, HA);

        base64_encode((uint8*)HA, SHA1_SIZE, HABase64, sizeof(HABase64));

        offset += snprintf(p_buf+offset, mlen-offset, "<s:Header>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:Action>%s</wsa:Action>", 
            action);
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:MessageID>urn:uuid:%s</wsa:MessageID>", 
            onvif_uuid_create(uuid, sizeof(uuid)));
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:ReplyTo>"
            "<wsa:Address>http://www.w3.org/2005/08/addressing/anonymous</wsa:Address>"
            "</wsa:ReplyTo>");

        if (to != NULL)
        {
            offset += snprintf(p_buf+offset, mlen-offset, "<wsa:To>%s</wsa:To>", to);
        }

        if (parameters && parameters[0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, "%s", parameters);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsse:Security>"
            "<wsse:UsernameToken>"
            "<wsse:Username>%s</wsse:Username>"
            "<wsse:Password "
            "Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
            "%s</wsse:Password>"
            "<wsse:Nonce "
            "EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
            "%s</wsse:Nonce>"
            "<wsu:Created>%s</wsu:Created>"
            "</wsse:UsernameToken>"
            "</wsse:Security>", 
            p_dev->username, 
            HABase64, 
            nonceBase64, 
            created);

        offset += snprintf(p_buf+offset, mlen-offset, "</s:Header>");
    }
    else if (to != NULL)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<s:Header>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:Action>%s</wsa:Action>", 
            action);
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:MessageID>urn:uuid:%s</wsa:MessageID>", 
            onvif_uuid_create(uuid, sizeof(uuid)));
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:ReplyTo>"
            "<wsa:Address>http://www.w3.org/2005/08/addressing/anonymous</wsa:Address>"
            "</wsa:ReplyTo>");
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsa:To>%s</wsa:To>", 
            to);

        if (parameters && parameters[0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, "%s", parameters);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</s:Header>");
    }

    return offset;
}


/***************************************************************************************/

int build_User_xml(char * p_buf, int mlen, onvif_User * p_req)
{
    int offset = 0;

    offset = snprintf(p_buf+offset, mlen-offset, 
        "<tds:User>"
            "<tt:Username>%s</tt:Username>"
            "<tt:Password>%s</tt:Password>"
            "<tt:UserLevel>%s</tt:UserLevel>"
        "</tds:User>",
        p_req->Username,
        p_req->Password,
        onvif_UserLevelToString(p_req->UserLevel));

    return offset;        
}

int build_RemoteUser_xml(char * p_buf, int mlen, onvif_RemoteUser * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Username>%s<tt:Username>", 
        p_req->Username);
    
    if (p_req->PasswordFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Password>%s<tt:Password>", 
            p_req->Password);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:UseDerivedPassword>%s<tt:UseDerivedPassword>", 
        p_req->UseDerivedPassword ? "true" : "false");

    return offset;
}

int build_Dot11PSKSet_xml(char * p_buf, int mlen, onvif_Dot11PSKSet * p_req)
{
    int offset = 0;
    
    if (p_req->KeyFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Key>%s</tt:Key>", 
            p_req->Key);
    }

    if (p_req->PassphraseFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Passphrase>%s</tt:Passphrase>", 
            p_req->Passphrase);
    }

    return offset;
}

int build_Dot11Configuration_xml(char * p_buf, int mlen, onvif_Dot11Configuration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:SSID>%s</tt:SSID>"
        "<tt:Mode>%s</tt:Mode>"
        "<tt:Alias>%s</tt:Alias>"
        "<tt:Priority>%d</tt:Priority>", 
        p_req->SSID,
        onvif_Dot11StationModeToString(p_req->Mode),
        p_req->Alias,
        p_req->Priority);

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:Security>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>", 
        onvif_Dot11SecurityModeToString(p_req->Security.Mode));

    if (p_req->Security.AlgorithmFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Algorithm>%s</tt:Algorithm>", 
            onvif_Dot11CipherToString(p_req->Security.Algorithm));
    }

    if (p_req->Security.PSKFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:PSK>");
        offset += build_Dot11PSKSet_xml(p_buf+offset, mlen-offset, &p_req->Security.PSK);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:PSK>");
    }

    if (p_req->Security.Dot1XFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Dot1X>%s</tt:Dot1X>", 
            p_req->Security.Dot1X);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:Security>");
    
    return offset;
}

int build_IPv4NetworkInterface_xml(char * p_buf, int mlen, onvif_IPv4NetworkInterface * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Enabled>%s</tt:Enabled>", 
        p_req->Enabled ? "true" : "false");

    if (!p_req->Config.DHCP)
    {
        uint32 i;

        for (i = 0; i < p_req->Config.sizeAddress; i++)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Manual>"
                    "<tt:Address>%s</tt:Address>"
                    "<tt:PrefixLength>%d</tt:PrefixLength>"
                "</tt:Manual>",
                p_req->Config.Address[i].Address, 
                p_req->Config.Address[i].PrefixLength);
        }
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:DHCP>%s</tt:DHCP>",
        p_req->Config.DHCP ? "true" : "false");

    return offset;
}

int build_IPv6NetworkInterface_xml(char * p_buf, int mlen, onvif_IPv6NetworkInterface * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Enabled>%s</tt:Enabled>"
        "<tt:AcceptRouterAdvert>%s</tt:AcceptRouterAdvert>", 
        p_req->Enabled ? "true" : "false",
        p_req->Config.AcceptRouterAdvert ? "true" : "false");

    if (p_req->Config.DHCP == IPv6DHCPConfiguration_Off)
    {
        uint32 i;

        for (i = 0; i < p_req->Config.sizeAddress; i++)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Manual>"
                    "<tt:Address>%s</tt:Address>"
                    "<tt:PrefixLength>%d</tt:PrefixLength>"
                "</tt:Manual>",
                p_req->Config.Address[i].Address, 
                p_req->Config.Address[i].PrefixLength);
        }
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:DHCP>%s</tt:DHCP>",
        onvif_IPv6DHCPConfigurationToString(p_req->Config.DHCP));

    return offset;
}

int build_NetworkInterfaceExtension_xml(char * p_buf, int mlen, onvif_NetworkInterfaceExtension * p_req)
{
    uint32 i;
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:InterfaceType>%d</tt:InterfaceType>", 
        p_req->InterfaceType);

    for (i = 0; i < p_req->sizeDot11; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Dot11>");
        offset += build_Dot11Configuration_xml(p_buf+offset, mlen-offset, &p_req->Dot11[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Dot11>");
    }

    return offset;
}

int build_DateTime_xml(char * p_buf, int mlen, onvif_DateTime * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Time>"
            "<tt:Hour>%d</tt:Hour>"
            "<tt:Minute>%d</tt:Minute>"
            "<tt:Second>%d</tt:Second>"
        "</tt:Time>",
        p_req->Time.Hour, 
        p_req->Time.Minute, 
        p_req->Time.Second);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Date>"
            "<tt:Year>%d</tt:Year>"
            "<tt:Month>%d</tt:Month>"
            "<tt:Day>%d</tt:Day>"
        "</tt:Date>",
        p_req->Date.Year, 
        p_req->Date.Month,
        p_req->Date.Day);
            
    return offset;
}

int build_LocationEntity_xml(char * p_buf, int mlen, onvif_LocationEntity * p_req)
{
    int offset = 0;
    char buff[32];

    if (p_req->GeoLocationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:GeoLocation");

        if (p_req->GeoLocation.lonFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " lon=\"%s\"", 
                onvif_format_float_num((float)p_req->GeoLocation.lon, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->GeoLocation.latFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " lat=\"%s\"", 
                onvif_format_float_num((float)p_req->GeoLocation.lat, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->GeoLocation.elevationFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " elevation=\"%s\"", 
                onvif_format_float_num(p_req->GeoLocation.elevation, 6, buff, sizeof(buff)-1));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "></tt:GeoLocation>");
    }

    if (p_req->GeoOrientationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:GeoOrientation");

        if (p_req->GeoOrientation.rollFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " roll=\"%s\"", 
                onvif_format_float_num(p_req->GeoOrientation.roll, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->GeoOrientation.pitchFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " pitch=\"%s\"", 
                onvif_format_float_num(p_req->GeoOrientation.pitch, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->GeoOrientation.yawFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " yaw=\"%s\"", 
                onvif_format_float_num(p_req->GeoOrientation.yaw, 6, buff, sizeof(buff)-1));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "></tt:GeoOrientation>");
    }

    if (p_req->LocalLocationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:LocalLocation");

        if (p_req->LocalLocation.xFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " x=\"%s\"", 
                onvif_format_float_num(p_req->LocalLocation.x, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->LocalLocation.yFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " y=\"%s\"", 
                onvif_format_float_num(p_req->LocalLocation.y, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->LocalLocation.zFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " z=\"%s\"", 
                onvif_format_float_num(p_req->LocalLocation.z, 6, buff, sizeof(buff)-1));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "></tt:LocalLocation>");
    }

    if (p_req->LocalOrientationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:LocalOrientation");

        if (p_req->LocalOrientation.panFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " pan=\"%s\"", 
                onvif_format_float_num(p_req->LocalOrientation.pan, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->LocalOrientation.tiltFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " tilt=\"%s\"", 
                onvif_format_float_num(p_req->LocalOrientation.tilt, 6, buff, sizeof(buff)-1));
        }
        
        if (p_req->LocalOrientation.rollFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " roll=\"%s\"", 
                onvif_format_float_num(p_req->LocalOrientation.roll, 6, buff, sizeof(buff)-1));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "></tt:LocalOrientation>");
    }

    return offset;    
}

int build_UserCredential_xml(char * p_buf, int mlen, onvif_UserCredential * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:UserName>%s</tds:UserName>", 
        p_req->UserName);

    if (p_req->PasswordFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tds:Password>%s</tds:Password>", 
            p_req->Password);
    }

    return offset;
}

int build_StorageConfigurationData_xml(char * p_buf, int mlen, onvif_StorageConfigurationData * p_req)
{
    int offset = 0;
    
    if (p_req->LocalPathFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tds:LocalPath>%s</tds:LocalPath>", 
            p_req->LocalPath);
    }

    if (p_req->StorageUriFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tds:StorageUri>%s</tds:StorageUri>", 
            p_req->StorageUri);
    }

    if (p_req->UserFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:User>");
        offset += build_UserCredential_xml(p_buf+offset, mlen-offset, &p_req->User);
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:User>");
    }

    if (p_req->CertPathValidationPolicyIDFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tds:CertPathValidationPolicyID>%s</tds:CertPathValidationPolicyID>", 
            p_req->CertPathValidationPolicyID);
    }

    return offset;
}

int build_StorageConfiguration_xml(char * p_buf, int mlen, onvif_StorageConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:Data type=\"%s\">", 
        p_req->Data.type);
    offset += build_StorageConfigurationData_xml(p_buf+offset, mlen-offset, &p_req->Data);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:Data>");

    return offset;
}

int build_tds_GetCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_GetCapabilities_REQ * p_req = (tds_GetCapabilities_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:GetCapabilities>"
            "<tds:Category>%s</tds:Category>"
        "</tds:GetCapabilities>", 
        p_req ? onvif_CapabilityCategoryToString(p_req->Category) : "All");
        
    return offset;
}

int build_tds_GetServices_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_GetServices_REQ * p_req = (tds_GetServices_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:GetServices>"
            "<tds:IncludeCapability>%s</tds:IncludeCapability>"
        "</tds:GetServices>", 
        p_req ? (p_req->IncludeCapability ? "true" : "false") : "false");
        
    return offset;
}

int build_tds_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetServiceCapabilities />");
    return offset;
}

int build_tds_GetDeviceInformation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetDeviceInformation />");
    return offset;
}

int build_tds_GetUsers_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetUsers />");
    return offset;
}

int build_tds_CreateUsers_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_CreateUsers_REQ * p_req = (tds_CreateUsers_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:CreateUsers>");
    offset += build_User_xml(p_buf+offset, mlen-offset, &p_req->User);
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:CreateUsers>");

    return offset;
}

int build_tds_DeleteUsers_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_DeleteUsers_REQ * p_req = (tds_DeleteUsers_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:DeleteUsers>"
            "<tds:Username>%s</tds:Username>"
        "</tds:DeleteUsers>",
        p_req->Username);

    return offset;
}

int build_tds_SetUser_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetUser_REQ * p_req = (tds_SetUser_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetUser>");
    offset += build_User_xml(p_buf+offset, mlen-offset, &p_req->User);
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetUser>");

    return offset;
}

int build_tds_GetRemoteUser_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetRemoteUser />");
    return offset;
}

int build_tds_SetRemoteUser_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetRemoteUser_REQ * p_req = (tds_SetRemoteUser_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetRemoteUser>");

    if (p_req->RemoteUserFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:RemoteUser>");
        offset += build_RemoteUser_xml(p_buf+offset, mlen-offset, &p_req->RemoteUser);
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:RemoteUser>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetRemoteUser>");
    
    return offset;
}

int build_tds_GetNetworkInterfaces_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetNetworkInterfaces />");
    return offset;
}

int build_tds_SetNetworkInterfaces_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetNetworkInterfaces_REQ * p_req = (tds_SetNetworkInterfaces_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetNetworkInterfaces>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:InterfaceToken>%s</tds:InterfaceToken>", 
        p_req->NetworkInterface.token);

    offset += snprintf(p_buf+offset, mlen-offset, "<tds:NetworkInterface>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Enabled>%s</tt:Enabled>", 
        p_req->NetworkInterface.Enabled ? "true" : "false");

    if (p_req->NetworkInterface.InfoFlag && p_req->NetworkInterface.Info.MTUFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MTU>%d</tt:MTU>", 
            p_req->NetworkInterface.Info.MTU);
    }

    if (p_req->NetworkInterface.IPv4Flag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:IPv4>");
        offset += build_IPv4NetworkInterface_xml(p_buf+offset, mlen-offset, &p_req->NetworkInterface.IPv4);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:IPv4>");
    }

    if (p_req->NetworkInterface.IPv6Flag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:IPv6>");
        offset += build_IPv6NetworkInterface_xml(p_buf+offset, mlen-offset, &p_req->NetworkInterface.IPv6);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:IPv6>");
    }

    if (p_req->NetworkInterface.ExtensionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Extension>");
        offset += build_NetworkInterfaceExtension_xml(p_buf+offset, mlen-offset, &p_req->NetworkInterface.Extension);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Extension>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:NetworkInterface>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetNetworkInterfaces>");
    
    return offset;
}

int build_tds_GetNTP_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetNTP />");
    return offset;
}

int build_tds_SetNTP_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetNTP_REQ * p_req = (tds_SetNTP_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetNTP>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:FromDHCP>%s</tds:FromDHCP>", 
        p_req->NTPInformation.FromDHCP ? "true" : "false");

    if (p_req->NTPInformation.FromDHCP == FALSE)
    {
        uint32 i;
        for (i = 0; i < ARRAY_SIZE(p_req->NTPInformation.NTPServer); i++)
        {
            if (p_req->NTPInformation.NTPServer[i][0] == '\0')
            {
                continue;
            }

            if (is_ipv4_address(p_req->NTPInformation.NTPServer[i]))
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tds:NTPManual>"
                        "<tt:Type>IPv4</tt:Type>"
                        "<tt:IPv4Address>%s</tt:IPv4Address>"
                    "</tds:NTPManual>", 
                    p_req->NTPInformation.NTPServer[i]);
            }
            else if (is_ipv6_address(p_req->NTPInformation.NTPServer[i]))
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tds:NTPManual>"
                        "<tt:Type>IPv6</tt:Type>"
                        "<tt:IPv6Address>%s</tt:IPv6Address>"
                    "</tds:NTPManual>", 
                    p_req->NTPInformation.NTPServer[i]);
            }
            else
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tds:NTPManual>"
                        "<tt:Type>DNS</tt:Type>"
                        "<tt:DNSname>%s</tt:DNSname>"
                    "</tds:NTPManual>", 
                    p_req->NTPInformation.NTPServer[i]);
            }
        }    
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetNTP>");
    
    return offset;
}

int build_tds_GetHostname_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetHostname />");
    return offset;
}

int build_tds_SetHostname_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetHostname_REQ * p_req = (tds_SetHostname_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetHostname>"
            "<tds:Name>%s</tds:Name>"
        "</tds:SetHostname>", 
        p_req->Name);
    
    return offset;
}

int build_tds_SetHostnameFromDHCP_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetHostnameFromDHCP_REQ * p_req = (tds_SetHostnameFromDHCP_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetHostnameFromDHCP>"
            "<tds:FromDHCP>%s</tds:FromDHCP>"
        "</tds:SetHostnameFromDHCP>", 
        p_req ? (p_req->FromDHCP ? "true" : "false") : "false");
    
    return offset;    
}

int build_tds_GetDNS_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetDNS />");
    return offset;
}

int build_tds_SetDNS_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tds_SetDNS_REQ * p_req = (tds_SetDNS_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetDNS>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:FromDHCP>%s</tds:FromDHCP>", 
        p_req->DNSInformation.FromDHCP ? "true" : "false");

    if (p_req->DNSInformation.SearchDomainFlag)
    {
        for (i = 0; i < ARRAY_SIZE(p_req->DNSInformation.SearchDomain); i++)
        {
            if (p_req->DNSInformation.SearchDomain[i][0] == '\0')
            {
                continue;
            }
            
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tds:SearchDomain>%s</tds:SearchDomain>", 
                p_req->DNSInformation.SearchDomain[i]);
        }
    }

    if (p_req->DNSInformation.FromDHCP == FALSE)
    {
        for (i = 0; i < ARRAY_SIZE(p_req->DNSInformation.DNSServer); i++)
        {
            if (p_req->DNSInformation.DNSServer[i][0] == '\0')
            {
                continue;
            }

            if (is_ipv6_address(p_req->DNSInformation.DNSServer[i]))
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tds:DNSManual>"
                        "<tt:Type>IPv6</tt:Type>"
                        "<tt:IPv6Address>%s</tt:IPv6Address>"
                    "</tds:DNSManual>", 
                    p_req->DNSInformation.DNSServer[i]);
            }
            else
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tds:DNSManual>"
                        "<tt:Type>IPv4</tt:Type>"
                        "<tt:IPv4Address>%s</tt:IPv4Address>"
                    "</tds:DNSManual>", 
                    p_req->DNSInformation.DNSServer[i]);
            }
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetDNS>");
    
    return offset;
}

int build_tds_GetDynamicDNS_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetDynamicDNS />");
    return offset;        
}

int build_tds_SetDynamicDNS_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetDynamicDNS_REQ * p_req = (tds_SetDynamicDNS_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetDynamicDNS>"
            "<tds:Type>%s</tds:Type>"
            "<tds:Name>%s</tds:Name>"
            "<tds:TTL>%d</tds:TTL>"
        "</tds:SetDynamicDNS>", 
        onvif_DynamicDNSTypeToString(p_req->DynamicDNSInformation.Type), 
        p_req->DynamicDNSInformation.Name, 
        p_req->DynamicDNSInformation.TTL);
    
    return offset;
}

int build_tds_GetNetworkProtocols_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetNetworkProtocols />");
    return offset;
}

int build_tds_SetNetworkProtocols_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;    
    int offset = 0;
    tds_SetNetworkProtocols_REQ * p_req = (tds_SetNetworkProtocols_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetNetworkProtocols>");

    if (p_req->NetworkProtocol.HTTPFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:NetworkProtocols>");

        offset += snprintf(p_buf+offset, mlen-offset,     
            "<tt:Name>HTTP</tt:Name>"
            "<tt:Enabled>%s</tt:Enabled>", 
            p_req->NetworkProtocol.HTTPEnabled ? "true" : "false");

        for (i = 0; i < ARRAY_SIZE(p_req->NetworkProtocol.HTTPPort); i++)
        {
            if (p_req->NetworkProtocol.HTTPPort[i] != 0)
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tt:Port>%d</tt:Port>", 
                    p_req->NetworkProtocol.HTTPPort[i]);
            }
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:NetworkProtocols>");
    }

    if (p_req->NetworkProtocol.HTTPSFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:NetworkProtocols>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Name>HTTPS</tt:Name>"
            "<tt:Enabled>%s</tt:Enabled>", 
            p_req->NetworkProtocol.HTTPSEnabled ? "true" : "false");

        for (i = 0; i < ARRAY_SIZE(p_req->NetworkProtocol.HTTPSPort); i++)
        {
            if (p_req->NetworkProtocol.HTTPSPort[i] != 0)
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tt:Port>%d</tt:Port>", 
                    p_req->NetworkProtocol.HTTPSPort[i]);
            }
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:NetworkProtocols>");
    }

    if (p_req->NetworkProtocol.RTSPFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:NetworkProtocols>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Name>RTSP</tt:Name>"
            "<tt:Enabled>%s</tt:Enabled>", 
            p_req->NetworkProtocol.RTSPEnabled ? "true" : "false");

        for (i = 0; i < ARRAY_SIZE(p_req->NetworkProtocol.RTSPPort); i++)
        {
            if (p_req->NetworkProtocol.RTSPPort[i] != 0)
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tt:Port>%d</tt:Port>", 
                    p_req->NetworkProtocol.RTSPPort[i]);
            }
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:NetworkProtocols>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetNetworkProtocols>");
    
    return offset;
}

int build_tds_GetDiscoveryMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetDiscoveryMode />");
    return offset;
}

int build_tds_SetDiscoveryMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetDiscoveryMode_REQ * p_req = (tds_SetDiscoveryMode_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetDiscoveryMode>"
            "<tds:DiscoveryMode>%s</tds:DiscoveryMode>"
           "</tds:SetDiscoveryMode>",
           p_req ? onvif_DiscoveryModeToString(p_req->DiscoveryMode) : "Discoverable");
    
    return offset;
}

int build_tds_GetNetworkDefaultGateway_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetNetworkDefaultGateway />");
    return offset;
}

int build_tds_SetNetworkDefaultGateway_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tds_SetNetworkDefaultGateway_REQ * p_req = (tds_SetNetworkDefaultGateway_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetNetworkDefaultGateway>");

    for (i = 0; i < ARRAY_SIZE(p_req->IPv4Address); i++)
    {
        if (p_req->IPv4Address[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tds:IPv4Address>%s</tds:IPv4Address>", 
                p_req->IPv4Address[i]);
        }
    }

    for (i = 0; i < ARRAY_SIZE(p_req->IPv6Address); i++)
    {
        if (p_req->IPv6Address[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tds:IPv6Address>%s</tds:IPv6Address>", 
                p_req->IPv6Address[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetNetworkDefaultGateway>");
    
    return offset;
}

int build_tds_GetZeroConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetZeroConfiguration />");
    return offset;
}

int build_tds_SetZeroConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetZeroConfiguration_REQ * p_req = (tds_SetZeroConfiguration_REQ *) argv;    
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetZeroConfiguration>"
            "<tds:InterfaceToken>%s</tds:InterfaceToken>"
            "<tds:Enabled>%s</tds:Enabled>"
        "</tds:SetZeroConfiguration>",
        p_req->InterfaceToken,
        p_req->Enabled ? "true" : "false");
    
    return offset;
}

int build_tds_GetEndpointReference_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetEndpointReference />");
    return offset;
}

int build_tds_SendAuxiliaryCommand_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SendAuxiliaryCommand_REQ * p_req = (tds_SendAuxiliaryCommand_REQ *) argv;    
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SendAuxiliaryCommand>"
            "<tds:AuxiliaryCommand>%s</tds:AuxiliaryCommand>"
        "</tds:SendAuxiliaryCommand>",
        p_req->AuxiliaryCommand);
    
    return offset;
}

int build_tds_GetRelayOutputs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetRelayOutputs />");
    return offset;
}

int build_tds_SetRelayOutputSettings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetRelayOutputSettings_REQ * p_req = (tds_SetRelayOutputSettings_REQ *) argv;    
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetRelayOutputSettings>"
            "<tds:RelayOutputToken>%s</tds:RelayOutputToken>"
            "<tds:Properties>"
                "<tt:Mode>%s</tt:Mode>"
                "<tt:DelayTime>PT%dS</tt:DelayTime>"
                "<tt:IdleState>%s</tt:IdleState>"
            "</tds:Properties>"
        "</tds:SetRelayOutputSettings>",
        p_req->RelayOutputToken,
        onvif_RelayModeToString(p_req->Properties.Mode),
        p_req->Properties.DelayTime,
        onvif_RelayIdleStateToString(p_req->Properties.IdleState));
    
    return offset;
}

int build_tds_SetRelayOutputState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetRelayOutputState_REQ * p_req = (tds_SetRelayOutputState_REQ *) argv;    
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetRelayOutputState>"
            "<tds:RelayOutputToken>%s</tds:RelayOutputToken>"
            "<tds:LogicalState>%s</tds:LogicalState>"
        "</tds:SetRelayOutputState>",
        p_req->RelayOutputToken,
        onvif_RelayLogicalStateToString(p_req->LogicalState));
    
    return offset;
}

int build_tds_GetSystemDateAndTime_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetSystemDateAndTime />");
    return offset;
}

int build_tds_SetSystemDateAndTime_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetSystemDateAndTime_REQ * p_req = (tds_SetSystemDateAndTime_REQ *) argv;    
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetSystemDateAndTime>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:DateTimeType>%s</tds:DateTimeType>"
        "<tds:DaylightSavings>%s</tds:DaylightSavings>",
        onvif_SetDateTimeTypeToString(p_req->SystemDateTime.DateTimeType),
        p_req->SystemDateTime.DaylightSavings ? "true" : "false");
        
    if (p_req->SystemDateTime.TimeZoneFlag && p_req->SystemDateTime.TimeZone.TZ[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tds:TimeZone><tt:TZ>%s</tt:TZ></tds:TimeZone>", 
            p_req->SystemDateTime.TimeZone.TZ);
    }

    if (p_req->SystemDateTime.DateTimeType == SetDateTimeType_Manual)
    {        
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:UTCDateTime>");
        offset += build_DateTime_xml(p_buf+offset, mlen-offset, &p_req->UTCDateTime);
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:UTCDateTime>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetSystemDateAndTime>");
    
    return offset;
}

int build_tds_SystemReboot_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SystemReboot />");
    return offset;
}

int build_tds_SetSystemFactoryDefault_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetSystemFactoryDefault_REQ * p_req = (tds_SetSystemFactoryDefault_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetSystemFactoryDefault>"
            "<tds:FactoryDefault>%s</tds:FactoryDefault>"
        "</tds:SetSystemFactoryDefault>", 
        p_req ? (onvif_FactoryDefaultTypeToString(p_req->FactoryDefault)) : "Soft");
    
    return offset;
}

int build_tds_GetSystemLog_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_GetSystemLog_REQ * p_req = (tds_GetSystemLog_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:GetSystemLog>"
            "<tds:LogType>%s</tds:LogType>"
        "</tds:GetSystemLog>", 
        p_req ? (onvif_SystemLogTypeToString(p_req->LogType)) : "System");
    
    return offset;
}
        
int build_tds_GetScopes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetScopes />");    
    return offset;
}
        
int build_tds_SetScopes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tds_SetScopes_REQ * p_req = (tds_SetScopes_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetScopes>");
    
    for (i = 0; i < p_req->sizeScopes; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Scopes>%s</tt:Scopes>", 
            p_req->Scopes[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetScopes>");    
    
    return offset;
}
        
int build_tds_AddScopes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tds_AddScopes_REQ * p_req = (tds_AddScopes_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:AddScopes>");
    
    for (i = 0; i < p_req->sizeScopeItem; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:ScopeItem>%s</tt:ScopeItem>", 
            p_req->ScopeItem[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:AddScopes>");    
    
    return offset;
}
        
int build_tds_RemoveScopes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tds_RemoveScopes_REQ * p_req = (tds_RemoveScopes_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:RemoveScopes>");
    
    for (i = 0; i < p_req->sizeScopeItem; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:ScopeItem>%s</tt:ScopeItem>", 
            p_req->ScopeItem[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:RemoveScopes>");    
    
    return offset;
}

int build_tds_StartFirmwareUpgrade_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:StartFirmwareUpgrade />");
    return offset;
}

int build_tds_GetSystemUris_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetSystemUris />");
    return offset;
}

int build_tds_StartSystemRestore_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:StartSystemRestore />");
    return offset;
}

int build_tds_GetWsdlUrl_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetWsdlUrl />");
    return offset;
}

int build_tds_GetDot11Capabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetDot11Capabilities />");
    return offset;
}

int build_tds_GetDot11Status_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_GetDot11Status_REQ * p_req = (tds_GetDot11Status_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:GetDot11Status>"
            "<tds:InterfaceToken>%s</tds:InterfaceToken>"
        "</tds:GetDot11Status>", 
        p_req->InterfaceToken);
    
    return offset;
}

int build_tds_ScanAvailableDot11Networks_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_ScanAvailableDot11Networks_REQ * p_req = (tds_ScanAvailableDot11Networks_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:ScanAvailableDot11Networks>"
            "<tds:InterfaceToken>%s</tds:InterfaceToken>"
        "</tds:ScanAvailableDot11Networks>", 
        p_req->InterfaceToken);
    
    return offset;
}

int build_tds_GetGeoLocation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetGeoLocation />");
    return offset;
}

int build_tds_SetGeoLocation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetGeoLocation_REQ * p_req = (tds_SetGeoLocation_REQ *) argv;
    LocationEntityList * p_info = p_req->Location;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetGeoLocation>");
    
    while (p_info)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:Location");

        if (p_info->Location.EntityFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Entity=\"%s\"", 
                p_info->Location.Entity);
        }
        
        if (p_info->Location.TokenFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Token=\"%s\"", 
                p_info->Location.Token);
        }
        
        if (p_info->Location.FixedFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Fixed=\"%s\"", 
                p_info->Location.Fixed ? "true" : "false");
        }
        
        if (p_info->Location.GeoSourceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " GeoSource=\"%s\"", 
                p_info->Location.GeoSource);
        }
        
        if (p_info->Location.AutoGeoFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " AutoGeo=\"%s\"", 
                p_info->Location.AutoGeo ? "true" : "false");
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, ">");
        
        offset += build_LocationEntity_xml(p_buf+offset, mlen-offset, &p_info->Location);
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:Location>");

        p_info = p_info->next;
    }    

    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetGeoLocation>");
    
    return offset;
}

int build_tds_DeleteGeoLocation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_DeleteGeoLocation_REQ * p_req = (tds_DeleteGeoLocation_REQ *) argv;
    LocationEntityList * p_info = p_req->Location;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:DeleteGeoLocation>");

    while (p_info)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tds:Location");

        if (p_info->Location.EntityFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Entity=\"%s\"", 
                p_info->Location.Entity);
        }
        
        if (p_info->Location.TokenFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Token=\"%s\"", 
                p_info->Location.Token);
        }
        
        if (p_info->Location.FixedFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Fixed=\"%s\"", 
                p_info->Location.Fixed ? "true" : "false");
        }
        
        if (p_info->Location.GeoSourceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " GeoSource=\"%s\"", 
                p_info->Location.GeoSource);
        }
        
        if (p_info->Location.AutoGeoFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " AutoGeo=\"%s\"", 
                p_info->Location.AutoGeo ? "true" : "false");
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, ">");
        
        offset += build_LocationEntity_xml(p_buf+offset, mlen-offset, &p_info->Location);
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tds:Location>");

        p_info = p_info->next;
    }    

    offset += snprintf(p_buf+offset, mlen-offset, "</tds:DeleteGeoLocation>");
    
    return offset;
}

int build_tds_SetHashingAlgorithm_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetHashingAlgorithm_REQ * p_req = (tds_SetHashingAlgorithm_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetHashingAlgorithm>"
            "<tds:Algorithm>%s</tds:Algorithm>"
        "</tds:SetHashingAlgorithm>", 
        p_req->Algorithm);
    
    return offset;
}

#ifdef IPFILTER_SUPPORT

int build_IPAddressFilter_xml(char * p_buf, int mlen, onvif_IPAddressFilter * p_req)
{
    uint32 i;
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Type>%s</tt:Type>", 
        onvif_IPAddressFilterTypeToString(p_req->Type));

    for (i = 0; i < ARRAY_SIZE(p_req->IPv4Address); i++)
    {
        if (p_req->IPv4Address[i].Address[0] == '\0')
        {
            continue;
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:IPv4Address>"
                "<tt:Address>%s</tt:Address>"
                "<tt:PrefixLength>%d</tt:PrefixLength>"
            "</tt:IPv4Address>", 
            p_req->IPv4Address[i].Address,
            p_req->IPv4Address[i].PrefixLength);
    }

    for (i = 0; i < ARRAY_SIZE(p_req->IPv6Address); i++)
    {
        if (p_req->IPv6Address[i].Address[0] == '\0')
        {
            continue;
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:IPv6Address>"
                "<tt:Address>%s</tt:Address>"
                "<tt:PrefixLength>%d</tt:PrefixLength>"
            "</tt:IPv6Address>", 
            p_req->IPv6Address[i].Address,
            p_req->IPv6Address[i].PrefixLength);
    }
    
    return offset;
}

int build_tds_GetIPAddressFilter_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetIPAddressFilter />");
    return offset;
}

int build_tds_SetIPAddressFilter_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetIPAddressFilter_REQ * p_req = (tds_SetIPAddressFilter_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:SetIPAddressFilter>"
        "<tds:IPAddressFilter>");
    offset += build_IPAddressFilter_xml(p_buf+offset, mlen-offset, &p_req->IPAddressFilter);    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:IPAddressFilter>"
        "</tds:SetIPAddressFilter>");

    return offset;
}

int build_tds_AddIPAddressFilter_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_AddIPAddressFilter_REQ * p_req = (tds_AddIPAddressFilter_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:AddIPAddressFilter>"
        "<tds:IPAddressFilter>");
    offset += build_IPAddressFilter_xml(p_buf+offset, mlen-offset, &p_req->IPAddressFilter);    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:IPAddressFilter>"
        "</tds:AddIPAddressFilter>");

    return offset;
}

int build_tds_RemoveIPAddressFilter_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_RemoveIPAddressFilter_REQ * p_req = (tds_RemoveIPAddressFilter_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:RemoveIPAddressFilter>"
        "<tds:IPAddressFilter>");
    offset += build_IPAddressFilter_xml(p_buf+offset, mlen-offset, &p_req->IPAddressFilter);    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:IPAddressFilter>"
        "</tds:RemoveIPAddressFilter>");

    return offset;
}

#endif // end of IPFILTER_SUPPORT

int build_tds_GetAccessPolicy_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetAccessPolicy />");
    return offset;
}

int build_tds_SetAccessPolicy_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetAccessPolicy_REQ * p_req = (tds_SetAccessPolicy_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetAccessPolicy>");    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:PolicyFile");    
    if (p_req->PolicyFile.contentTypeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " xmime:contentType=\"%s\"", 
            p_req->PolicyFile.contentType);
    }
    offset += snprintf(p_buf+offset, mlen-offset, ">");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Data>%s</tt:Data>", 
        p_req->PolicyFile.Data.ptr);
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:PolicyFile");
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetAccessPolicy>");
    
    return offset;
}

int build_tds_GetStorageConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:GetStorageConfigurations />");
    return offset;
}

int build_tds_CreateStorageConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_CreateStorageConfiguration_REQ * p_req = (tds_CreateStorageConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:CreateStorageConfiguration>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:StorageConfiguration type=\"%s\">", 
        p_req->StorageConfiguration.type);
    offset += build_StorageConfigurationData_xml(p_buf+offset, mlen-offset, &p_req->StorageConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:StorageConfiguration>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:CreateStorageConfiguration>");
    
    return offset;
}

int build_tds_GetStorageConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_GetStorageConfiguration_REQ * p_req = (tds_GetStorageConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:GetStorageConfiguration>"
            "<tds:Token>%s</tds:Token>"
        "</tds:GetStorageConfiguration>", 
        p_req->Token);
    
    return offset;
}

int build_tds_SetStorageConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_SetStorageConfiguration_REQ * p_req = (tds_SetStorageConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tds:SetStorageConfiguration>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:StorageConfiguration token=\"%s\">", 
        p_req->StorageConfiguration.token);
    offset += build_StorageConfiguration_xml(p_buf+offset, mlen-offset, &p_req->StorageConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tds:StorageConfiguration>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tds:SetStorageConfiguration>");
    
    return offset;
}

int build_tds_DeleteStorageConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tds_DeleteStorageConfiguration_REQ * p_req = (tds_DeleteStorageConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tds:DeleteStorageConfiguration>"
            "<tds:Token>%s</tds:Token>"
        "</tds:DeleteStorageConfiguration>", 
        p_req->Token);
    
    return offset;
}

/***************************************************************************************/

int build_MulticastConfiguration_xml(char * p_buf, int mlen, onvif_MulticastConfiguration * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Multicast>"
            "<tt:Address>"
                "<tt:Type>IPv4</tt:Type>"
                "<tt:IPv4Address>%s</tt:IPv4Address>"
            "</tt:Address>"
            "<tt:Port>%d</tt:Port>"
            "<tt:TTL>%d</tt:TTL>"
            "<tt:AutoStart>%s</tt:AutoStart>"
        "</tt:Multicast>", 
        p_req->IPv4Address,
        p_req->Port,
        p_req->TTL,
        p_req->AutoStart ? "true" : "false");

    return offset;        
}

int build_SimpleItem_xml(char * p_buf, int mlen, onvif_SimpleItem * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:SimpleItem Name=\"%s\" Value=\"%s\" />",
        p_req->Name, p_req->Value);

    return offset;
}

int build_Config_xml(char * p_buf, int mlen, onvif_Config * p_req)
{
    int offset = 0;
    SimpleItemList * p_simpleitem;
    ElementItemList * p_elementitem;

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:Parameters>");

    p_simpleitem = p_req->Parameters.SimpleItem;
    while (p_simpleitem)
    {
        offset += build_SimpleItem_xml(p_buf+offset, mlen-offset, &p_simpleitem->SimpleItem);
        
        p_simpleitem = p_simpleitem->next;
    }

    p_elementitem = p_req->Parameters.ElementItem;
    while (p_elementitem)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:ElementItem Name=\"%s\">", 
            p_elementitem->ElementItem.Name);

        if (p_elementitem->ElementItem.AnyFlag && p_elementitem->ElementItem.Any)
        {
            offset += snprintf(p_buf+offset, mlen-offset, "%s", p_elementitem->ElementItem.Any);
        }

        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:ElementItem>");
        
        p_elementitem = p_elementitem->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:Parameters>");
    
    return offset;
}

int build_AnalyticsEngineConfiguration_xml(char * p_buf, int mlen, onvif_AnalyticsEngineConfiguration * p_req)
{
    int offset = 0;
    ConfigList * p_config;
    
    p_config = p_req->AnalyticsModule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:AnalyticsModule Name=\"%s\" Type=\"%s\" %s>\r\n", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:AnalyticsModule Name=\"%s\" Type=\"%s\">\r\n", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:AnalyticsModule>\r\n");

        p_config = p_config->next;
    }

    return offset;
}

int build_MetadataConfiguration_xml(char * p_buf, int mlen, onvif_MetadataConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset,
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>",
        p_req->Name,
        p_req->UseCount);

    if (p_req->PTZStatusFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,
            "<tt:PTZStatus>"
                "<tt:Status>%s</tt:Status>"
                "<tt:Position>%s</tt:Position>"
            "</tt:PTZStatus>",
            p_req->PTZStatus.Status ? "true" : "false",
            p_req->PTZStatus.Position ? "true" : "false");
    }

    if (p_req->EventsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,
            "<tt:Events>\r\n"
                "<tt:Filter>\r\n"
                    "<wsnt:TopicExpression Dialect=\"%s\">%s</wsnt:TopicExpression>\r\n"
                "</tt:Filter>\r\n"    
            "</tt:Events>\r\n",
            p_req->Events.Dialect,
            p_req->Events.TopicExpression);
    }
    
    if (p_req->AnalyticsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,
            "<tt:Analytics>%s</tt:Analytics>",
            p_req->Analytics ? "true" : "false");
    }

    offset += build_MulticastConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Multicast);

    offset += snprintf(p_buf+offset, mlen-offset,
            "<tt:SessionTimeout>PT%dS</tt:SessionTimeout>",
            p_req->SessionTimeout);

    if (p_req->AnalyticsEngineConfigurationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:AnalyticsEngineConfiguration>");
        offset += build_AnalyticsEngineConfiguration_xml(p_buf+offset, mlen-offset, &p_req->AnalyticsEngineConfiguration);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:AnalyticsEngineConfiguration>");
    }
    
    return offset;
}

int build_VideoSourceConfiguration_xml(char * p_buf, int mlen, onvif_VideoSourceConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:SourceToken>%s</tt:SourceToken>"
         "<tt:Bounds x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"></tt:Bounds>",
         p_req->Name,
         p_req->UseCount, 
         p_req->SourceToken,
         p_req->Bounds.x,
         p_req->Bounds.y,
         p_req->Bounds.width, 
         p_req->Bounds.height);

    if (p_req->ExtensionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Extension>");
        
        if (p_req->Extension.RotateFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, "<tt:Rotate>");

            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Mode>%s</tt:Mode>", 
                onvif_RotateModeToString(p_req->Extension.Rotate.Mode));

            if (p_req->Extension.Rotate.DegreeFlag)
            {
                offset += snprintf(p_buf+offset, mlen-offset, 
                    "<tt:Degree>%d</tt:Degree>",
                    p_req->Extension.Rotate.Degree);
            }
            
            offset += snprintf(p_buf+offset, mlen-offset, "</tt:Rotate>");
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Extension>");
    }

    return offset;
}

int build_AudioSourceConfiguration_xml(char * p_buf, int mlen, onvif_AudioSourceConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset,
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:SourceToken>%s</tt:SourceToken>",
         p_req->Name, 
         p_req->UseCount, 
         p_req->SourceToken);

     return offset;
}

int build_StreamSetup_xml(char * p_buf, int mlen, onvif_StreamSetup * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Stream>%s</tt:Stream>"
        "<tt:Transport>"
            "<tt:Protocol>%s</tt:Protocol>"
        "</tt:Transport>",
        onvif_StreamTypeToString(p_req->Stream),
        onvif_TransportProtocolToString(p_req->Transport.Protocol));
        
    return offset;
}

int build_AudioOutputConfiguration_xml(char * p_buf, int mlen, onvif_AudioOutputConfiguration * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:OutputToken>%s</tt:OutputToken>",
        p_req->Name,
        p_req->UseCount,
        p_req->OutputToken);

    if (p_req->SendPrimacyFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:SendPrimacy>%s</tt:SendPrimacy>",
            p_req->SendPrimacy);
    }
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:OutputLevel>%d</tt:OutputLevel>",
        p_req->OutputLevel);

    return offset;
}

int build_AudioDecoderConfiguration_xml(char * p_buf, int mlen, onvif_AudioDecoderConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>",
        p_req->Name,
        p_req->UseCount);

    return offset;        
}

int build_OSDColor_xml(char * p_buf, int mlen, onvif_OSDColor * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32], buff3[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Color X=\"%s\" Y=\"%s\" Z=\"%s\" Colorspace=\"\"></tt:Color>", 
        onvif_format_float_num(p_req->X, 6, buff1, sizeof(buff1)),
        onvif_format_float_num(p_req->Y, 6, buff2, sizeof(buff2)),
        onvif_format_float_num(p_req->Z, 6, buff3, sizeof(buff3)));
            
    return offset;
}

int build_OSDTextConfiguration_xml(char * p_buf, int mlen, onvif_OSDTextConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Type>%s</tt:Type>", 
        onvif_OSDTextTypeToString(p_req->Type));

    if (p_req->DateFormatFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:DateFormat>%s</tt:DateFormat>", 
            p_req->DateFormat);
    }
    
    if (p_req->TimeFormatFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:TimeFormat>%s</tt:TimeFormat>", 
            p_req->TimeFormat);
    }
    
    if (p_req->FontSizeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:FontSize>%d</tt:FontSize>", 
            p_req->FontSize);
    }
    
    if (p_req->FontColorFlag)
    {
        if (p_req->FontColor.TransparentFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:FontColor Transparent=\"%d\">", 
                p_req->FontColor.Transparent);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:FontColor>");
        }            
        offset += build_OSDColor_xml(p_buf+offset, mlen-offset, &p_req->FontColor);
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:FontColor>");                
    }

    if (p_req->BackgroundColorFlag)
    {
        if (p_req->BackgroundColor.TransparentFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:BackgroundColor Transparent=\"%d\">", 
                p_req->FontColor.Transparent);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:BackgroundColor>");
        }            
        offset += build_OSDColor_xml(p_buf+offset, mlen-offset, &p_req->BackgroundColor);
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:BackgroundColor>");                
    }

    if (p_req->PlainTextFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:PlainText>%s</tt:PlainText>", 
            p_req->PlainText);
    }
    
    return offset;
}

int build_OSDConfiguration_xml(char * p_buf, int mlen, onvif_OSDConfiguration * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:VideoSourceConfigurationToken>%s</tt:VideoSourceConfigurationToken>", 
        p_req->VideoSourceConfigurationToken);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Type>%s</tt:Type>", 
        onvif_OSDTypeToString(p_req->Type));

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:Position>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Type>%s</tt:Type>", 
        onvif_OSDPosTypeToString(p_req->Position.Type));

    if (p_req->Position.PosFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Pos x=\"%s\" y=\"%s\" />",
            onvif_format_float_num(p_req->Position.Pos.x, 2, buff1, sizeof(buff1)),
            onvif_format_float_num(p_req->Position.Pos.y, 2, buff2, sizeof(buff2)));
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:Position>");

    if (p_req->TextStringFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:TextString>");
        offset += build_OSDTextConfiguration_xml(p_buf+offset, mlen-offset, &p_req->TextString);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:TextString>");
    }

    if (p_req->ImageFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Image>"
                "<tt:ImgPath>%s</tt:ImgPath>"
            "</tt:Image>",
            p_req->Image.ImgPath);
    }    

    return offset;
}

int build_VideoAnalyticsConfiguration_xml(char * p_buf, int mlen, onvif_VideoAnalyticsConfiguration * p_req)
{
    int offset = 0;
    ConfigList * p_config;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>",
        p_req->Name, p_req->UseCount);

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:AnalyticsEngineConfiguration>");

    p_config = p_req->AnalyticsEngineConfiguration.AnalyticsModule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:AnalyticsModule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:AnalyticsModule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:AnalyticsModule>");

        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:AnalyticsEngineConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:RuleEngineConfiguration>");

    p_config = p_req->RuleEngineConfiguration.Rule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Rule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Rule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:Rule>");

        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:RuleEngineConfiguration>");
    
    return offset;
}

int build_trt_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetServiceCapabilities />");
    return offset;
}

int build_trt_GetVideoSources_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetVideoSources />");
    return offset;
}
        
int build_trt_GetAudioSources_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioSources />");
    return offset;
}

int build_trt_CreateProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_CreateProfile_REQ * p_req = (trt_CreateProfile_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:CreateProfile><trt:Name>%s</trt:Name>", 
        p_req->Name);

    if (p_req->TokenFlag && p_req->Token[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:Token>%s</trt:Token>", 
            p_req->Token);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:CreateProfile>");
    
    return offset;
}
        
int build_trt_GetProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetProfile_REQ * p_req = (trt_GetProfile_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetProfile>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetProfile>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_GetProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetProfiles />");
    return offset;
}

int build_trt_AddVideoEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddVideoEncoderConfiguration_REQ * p_req = (trt_AddVideoEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddVideoEncoderConfiguration>"
                "<trt:ProfileToken>%s</trt:ProfileToken>"
                "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddVideoEncoderConfiguration>",
        p_req->ProfileToken, 
        p_req->ConfigurationToken);
    
    return offset;
}

        
int build_trt_AddVideoSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddVideoSourceConfiguration_REQ * p_req = (trt_AddVideoSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddVideoSourceConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddVideoSourceConfiguration>", 
        p_req->ProfileToken, 
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_AddAudioEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddAudioEncoderConfiguration_REQ * p_req = (trt_AddAudioEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddAudioEncoderConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddAudioEncoderConfiguration>", 
        p_req->ProfileToken, 
        p_req->ConfigurationToken);
    
    return offset;
}
        
int build_trt_AddAudioSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddAudioSourceConfiguration_REQ * p_req = (trt_AddAudioSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddAudioSourceConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddAudioSourceConfiguration>", 
        p_req->ProfileToken, 
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_GetVideoSourceModes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoSourceModes_REQ * p_req = (trt_GetVideoSourceModes_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoSourceModes>"
            "<trt:VideoSourceToken>%s</trt:VideoSourceToken>"
        "</trt:GetVideoSourceModes>", 
        p_req->VideoSourceToken);
    
    return offset;
}

int build_trt_SetVideoSourceMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetVideoSourceMode_REQ * p_req = (trt_SetVideoSourceMode_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetVideoSourceMode>"
            "<trt:VideoSourceToken>%s</trt:VideoSourceToken>"
            "<trt:VideoSourceModeToken>%s</trt:VideoSourceModeToken>"
        "</trt:SetVideoSourceMode>", 
        p_req->VideoSourceToken, 
        p_req->VideoSourceModeToken);
    
    return offset;
}

int build_trt_AddPTZConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddPTZConfiguration_REQ * p_req = (trt_AddPTZConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddPTZConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddPTZConfiguration>", 
        p_req->ProfileToken, 
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_RemoveVideoEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveVideoEncoderConfiguration_REQ * p_req = (trt_RemoveVideoEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveVideoEncoderConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveVideoEncoderConfiguration>",
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_RemoveVideoSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveVideoSourceConfiguration_REQ * p_req = (trt_RemoveVideoSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveVideoSourceConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveVideoSourceConfiguration>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_RemoveAudioEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveAudioEncoderConfiguration_REQ * p_req = (trt_RemoveAudioEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveAudioEncoderConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveAudioEncoderConfiguration>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_RemoveAudioSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveAudioSourceConfiguration_REQ * p_req = (trt_RemoveAudioSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveAudioSourceConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveAudioSourceConfiguration>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_RemovePTZConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemovePTZConfiguration_REQ * p_req = (trt_RemovePTZConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemovePTZConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemovePTZConfiguration>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_DeleteProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_DeleteProfile_REQ * p_req = (trt_DeleteProfile_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:DeleteProfile>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:DeleteProfile>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_GetVideoSourceConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetVideoSourceConfigurations />");
    return offset;
}

int build_trt_GetVideoEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetVideoEncoderConfigurations />");
    return offset;
}

int build_trt_GetAudioSourceConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioSourceConfigurations />");
    return offset;
}

int build_trt_GetAudioEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioEncoderConfigurations />");
    return offset;
}
    
int build_trt_GetVideoSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoSourceConfiguration_REQ * p_req = (trt_GetVideoSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoSourceConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetVideoSourceConfiguration>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetVideoEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoEncoderConfiguration_REQ * p_req = (trt_GetVideoEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoEncoderConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetVideoEncoderConfiguration>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetAudioSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioSourceConfiguration_REQ * p_req = (trt_GetAudioSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioSourceConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetAudioSourceConfiguration>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetAudioEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioEncoderConfiguration_REQ * p_req = (trt_GetAudioEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioEncoderConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetAudioEncoderConfiguration>",
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_SetVideoSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetVideoSourceConfiguration_REQ * p_req = (trt_SetVideoSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetVideoSourceConfiguration>"
        "<trt:Configuration token=\"%s\">",
        p_req->VideoSourceConfiguration.token);
    offset += build_VideoSourceConfiguration_xml(p_buf+offset, mlen-offset, &p_req->VideoSourceConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>"
        "<trt:ForcePersistence>%s</trt:ForcePersistence>"
        "</trt:SetVideoSourceConfiguration>",
        p_req->ForcePersistence ? "true" : "false");

    return offset;
}

int build_trt_SetVideoEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetVideoEncoderConfiguration_REQ * p_req = (trt_SetVideoEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetVideoEncoderConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:Configuration token=\"%s\">", 
        p_req->VideoEncoderConfiguration.token);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:Encoding>%s</tt:Encoding>",
        p_req->VideoEncoderConfiguration.Name,
        p_req->VideoEncoderConfiguration.UseCount, 
        onvif_VideoEncodingToString(p_req->VideoEncoderConfiguration.Encoding));

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Resolution>"
            "<tt:Width>%d</tt:Width>"
            "<tt:Height>%d</tt:Height>"
        "</tt:Resolution>",
        p_req->VideoEncoderConfiguration.Resolution.Width,
        p_req->VideoEncoderConfiguration.Resolution.Height);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Quality>%d</tt:Quality>", 
        p_req->VideoEncoderConfiguration.Quality);

    if (p_req->VideoEncoderConfiguration.RateControlFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RateControl>"
                "<tt:FrameRateLimit>%d</tt:FrameRateLimit>"
                  "<tt:EncodingInterval>%d</tt:EncodingInterval>"
                  "<tt:BitrateLimit>%d</tt:BitrateLimit>"
              "</tt:RateControl>",
              p_req->VideoEncoderConfiguration.RateControl.FrameRateLimit, 
              p_req->VideoEncoderConfiguration.RateControl.EncodingInterval, 
              p_req->VideoEncoderConfiguration.RateControl.BitrateLimit);
    }
    
    if (p_req->VideoEncoderConfiguration.Encoding == VideoEncoding_H264)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:H264>"
                "<tt:GovLength>%d</tt:GovLength>"
                "<tt:H264Profile>%s</tt:H264Profile>"
            "</tt:H264>", 
            p_req->VideoEncoderConfiguration.H264.GovLength,
            onvif_H264ProfileToString(p_req->VideoEncoderConfiguration.H264.H264Profile));
    }
    
    if (p_req->VideoEncoderConfiguration.Encoding == VideoEncoding_MPEG4)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MPEG4>"
                "<tt:GovLength>%d</tt:GovLength>"
                "<tt:Mpeg4Profile>%s</tt:Mpeg4Profile>"
            "</tt:MPEG4>", 
            p_req->VideoEncoderConfiguration.MPEG4.GovLength,
            onvif_Mpeg4ProfileToString(p_req->VideoEncoderConfiguration.MPEG4.Mpeg4Profile));
    }

    offset += build_MulticastConfiguration_xml(p_buf+offset, mlen-offset, &p_req->VideoEncoderConfiguration.Multicast);

    offset += snprintf(p_buf+offset, mlen-offset,
        "<tt:SessionTimeout>PT%dS</tt:SessionTimeout>",
        p_req->VideoEncoderConfiguration.SessionTimeout);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>",
        p_req->ForcePersistence ? "true" : "false");
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:SetVideoEncoderConfiguration>");
    
    return offset;
}

int build_trt_SetAudioSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetAudioSourceConfiguration_REQ * p_req = (trt_SetAudioSourceConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetAudioSourceConfiguration>"
        "<trt:Configuration token=\"%s\">",
        p_req->AudioSourceConfiguration.token);
    offset += build_AudioSourceConfiguration_xml(p_buf+offset, mlen-offset, &p_req->AudioSourceConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>",
        p_req->ForcePersistence ? "true" : "false");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>"
        "</trt:SetAudioSourceConfiguration>");

    return offset;
}

int build_trt_SetAudioEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetAudioEncoderConfiguration_REQ * p_req = (trt_SetAudioEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetAudioEncoderConfiguration>"
            "<trt:Configuration token=\"%s\">"
                "<tt:Name>%s</tt:Name>"
                "<tt:UseCount>%d</tt:UseCount>"
                "<tt:Encoding>%s</tt:Encoding>"
                "<tt:Bitrate>%d</tt:Bitrate>"
                 "<tt:SampleRate>%d</tt:SampleRate>",
        p_req->AudioEncoderConfiguration.token, 
         p_req->AudioEncoderConfiguration.Name, 
         p_req->AudioEncoderConfiguration.UseCount, 
         onvif_AudioEncodingToString(p_req->AudioEncoderConfiguration.Encoding), 
         p_req->AudioEncoderConfiguration.Bitrate, 
         p_req->AudioEncoderConfiguration.SampleRate);

    offset += build_MulticastConfiguration_xml(p_buf+offset, mlen-offset, &p_req->AudioEncoderConfiguration.Multicast);

    offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:SessionTimeout>PT%dS</tt:SessionTimeout>"
            "</trt:Configuration>"
            "<trt:ForcePersistence>%s</trt:ForcePersistence>"
        "</trt:SetAudioEncoderConfiguration>",
        p_req->AudioEncoderConfiguration.SessionTimeout,
        p_req->ForcePersistence ? "true" : "false");
    
    return offset;
}

int build_trt_GetVideoSourceConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoSourceConfigurationOptions_REQ * p_req = (trt_GetVideoSourceConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoSourceConfigurationOptions>");

    if (p_req && p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>", 
            p_req->ConfigurationToken);
    }
    
    if (p_req && p_req->ProfileTokenFlag && p_req->ProfileToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>", 
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetVideoSourceConfigurationOptions>");

    return offset;
}

int build_trt_GetVideoEncoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoEncoderConfigurationOptions_REQ * p_req = (trt_GetVideoEncoderConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoEncoderConfigurationOptions>");

    if (p_req && p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>", 
            p_req->ConfigurationToken);
    }
    
    if (p_req && p_req->ProfileTokenFlag && p_req->ProfileToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>", 
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetVideoEncoderConfigurationOptions>");

    return offset;
}

int build_trt_GetAudioSourceConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioSourceConfigurationOptions_REQ * p_req = (trt_GetAudioSourceConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioSourceConfigurationOptions>");

    if (p_req && p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>", 
            p_req->ConfigurationToken);
    }
    
    if (p_req && p_req->ProfileTokenFlag && p_req->ProfileToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>", 
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetAudioSourceConfigurationOptions>");

    return offset;
}

int build_trt_GetAudioEncoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioEncoderConfigurationOptions_REQ * p_req = (trt_GetAudioEncoderConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioEncoderConfigurationOptions>");

    if (p_req && p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>", 
            p_req->ConfigurationToken);
    }

    if (p_req && p_req->ProfileTokenFlag && p_req->ProfileToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>", 
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetAudioEncoderConfigurationOptions>");

    return offset;
}

int build_trt_GetStreamUri_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetStreamUri_REQ * p_req = (trt_GetStreamUri_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetStreamUri>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:StreamSetup>");
    offset += build_StreamSetup_xml(p_buf+offset, mlen-offset, &p_req->StreamSetup);
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:StreamSetup>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ProfileToken>%s</trt:ProfileToken>",
        p_req->ProfileToken);
        
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:GetStreamUri>");

    return offset;
}

int build_trt_SetSynchronizationPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetSynchronizationPoint_REQ * p_req = (trt_SetSynchronizationPoint_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetSynchronizationPoint>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:SetSynchronizationPoint>", 
        p_req->ProfileToken);

    return offset;
}
        
int build_trt_GetSnapshotUri_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetSnapshotUri_REQ * p_req = (trt_GetSnapshotUri_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetSnapshotUri>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetSnapshotUri>", 
        p_req->ProfileToken);

    return offset;
}

int build_trt_GetGuaranteedNumberOfVideoEncoderInstances_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetGuaranteedNumberOfVideoEncoderInstances_REQ * p_req = (trt_GetGuaranteedNumberOfVideoEncoderInstances_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetGuaranteedNumberOfVideoEncoderInstances>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetGuaranteedNumberOfVideoEncoderInstances>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetAudioOutputs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioOutputs />");
    return offset;
}

int build_trt_GetAudioOutputConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioOutputConfigurations />");
    return offset;
}

int build_trt_GetAudioOutputConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioOutputConfiguration_REQ * p_req = (trt_GetAudioOutputConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioOutputConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetAudioOutputConfiguration>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetAudioOutputConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioOutputConfigurationOptions_REQ * p_req = (trt_GetAudioOutputConfigurationOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioOutputConfigurationOptions>");

    if (p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>",
            p_req->ConfigurationToken);
    }
    
    if (p_req->ProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>",
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetAudioOutputConfigurationOptions>");

    return offset;
}

int build_trt_SetAudioOutputConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetAudioOutputConfiguration_REQ * p_req = (trt_SetAudioOutputConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetAudioOutputConfiguration>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:Configuration token=\"%s\">", 
        p_req->Configuration.token);
    offset += build_AudioOutputConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>",
        p_req->ForcePersistence ? "true" : "false");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:SetAudioOutputConfiguration>");

    return offset;
}

int build_trt_GetAudioDecoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetAudioDecoderConfigurations />");
    return offset;
}

int build_trt_GetAudioDecoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioDecoderConfiguration_REQ * p_req = (trt_GetAudioDecoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioDecoderConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetAudioDecoderConfiguration>", 
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_GetAudioDecoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetAudioDecoderConfigurationOptions_REQ * p_req = (trt_GetAudioDecoderConfigurationOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetAudioDecoderConfigurationOptions>");

    if (p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>",
            p_req->ConfigurationToken);
    }
    
    if (p_req->ProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>",
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetAudioDecoderConfigurationOptions>");

    return offset;
}

int build_trt_SetAudioDecoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetAudioDecoderConfiguration_REQ * p_req = (trt_SetAudioDecoderConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetAudioDecoderConfiguration>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:Configuration token=\"%s\">", 
        p_req->Configuration.token);
    offset += build_AudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>",
        p_req->ForcePersistence ? "true" : "false");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:SetAudioDecoderConfiguration>");

    return offset;
}

int build_trt_AddAudioOutputConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddAudioOutputConfiguration_REQ * p_req = (trt_AddAudioOutputConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddAudioOutputConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddAudioOutputConfiguration>", 
        p_req->ProfileToken,
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_AddAudioDecoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddAudioDecoderConfiguration_REQ * p_req = (trt_AddAudioDecoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddAudioDecoderConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddAudioDecoderConfiguration>", 
        p_req->ProfileToken,
        p_req->ConfigurationToken);

    return offset;
}

int build_trt_RemoveAudioOutputConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveAudioOutputConfiguration_REQ * p_req = (trt_RemoveAudioOutputConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveAudioOutputConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveAudioOutputConfiguration>", 
        p_req->ProfileToken);

    return offset;
}

int build_trt_RemoveAudioDecoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveAudioDecoderConfiguration_REQ * p_req = (trt_RemoveAudioDecoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveAudioDecoderConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveAudioDecoderConfiguration>", 
        p_req->ProfileToken);

    return offset;
}

int build_trt_GetOSDs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetOSDs_REQ * p_req = (trt_GetOSDs_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetOSDs>");

    if (p_req && p_req->ConfigurationTokenFlag && p_req->ConfigurationToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>",
            p_req->ConfigurationToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:GetOSDs>");
    
    return offset;
}

int build_trt_GetOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetOSD_REQ * p_req = (trt_GetOSD_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetOSD>"
            "<trt:OSDToken>%s</trt:OSDToken>"
        "</trt:GetOSD>",
        p_req->OSDToken);
    
    return offset;
}

int build_trt_SetOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetOSD_REQ * p_req = (trt_SetOSD_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<trt:SetOSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:OSD token=\"%s\">", p_req->OSD.token);
    offset += build_OSDConfiguration_xml(p_buf+offset, mlen-offset, &p_req->OSD);
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:OSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:SetOSD>");
    
    return offset;
}

int build_trt_GetOSDOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetOSDOptions_REQ * p_req = (trt_GetOSDOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetOSDOptions>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetOSDOptions>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_CreateOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_CreateOSD_REQ * p_req = (trt_CreateOSD_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<trt:CreateOSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:OSD token=\"%s\">", p_req->OSD.token);
    offset += build_OSDConfiguration_xml(p_buf+offset, mlen-offset, &p_req->OSD);
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:OSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "</trt:CreateOSD>");
    
    return offset;
}

int build_trt_DeleteOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetOSD_REQ * p_req = (trt_GetOSD_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:DeleteOSD>"
            "<trt:OSDToken>%s</trt:OSDToken>"
        "</trt:DeleteOSD>",
        p_req->OSDToken);
    
    return offset;
}

int build_trt_GetVideoAnalyticsConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetVideoAnalyticsConfigurations />");
    return offset;
}

int build_trt_AddVideoAnalyticsConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddVideoAnalyticsConfiguration_REQ * p_req = (trt_AddVideoAnalyticsConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddVideoAnalyticsConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddVideoAnalyticsConfiguration>",
        p_req->ProfileToken,
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_GetVideoAnalyticsConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetVideoAnalyticsConfiguration_REQ * p_req = (trt_GetVideoAnalyticsConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetVideoAnalyticsConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetVideoAnalyticsConfiguration>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_RemoveVideoAnalyticsConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveVideoAnalyticsConfiguration_REQ * p_req = (trt_RemoveVideoAnalyticsConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveVideoAnalyticsConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveVideoAnalyticsConfiguration>",
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_SetVideoAnalyticsConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetVideoAnalyticsConfiguration_REQ * p_req = (trt_SetVideoAnalyticsConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetVideoAnalyticsConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:Configuration token=\"%s\">", 
        p_req->Configuration.token);
    offset += build_VideoAnalyticsConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>", 
        p_req->ForcePersistence ? "true" : "false");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:SetVideoAnalyticsConfiguration>");
    
    return offset;
}

int build_trt_GetMetadataConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trt:GetMetadataConfigurations />");
    return offset;
}

int build_trt_AddMetadataConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_AddMetadataConfiguration_REQ * p_req = (trt_AddMetadataConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:AddMetadataConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:AddMetadataConfiguration>",
        p_req->ProfileToken,
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_GetMetadataConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetMetadataConfiguration_REQ * p_req = (trt_GetMetadataConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetMetadataConfiguration>"
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>"
        "</trt:GetMetadataConfiguration>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_trt_RemoveMetadataConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_RemoveMetadataConfiguration_REQ * p_req = (trt_RemoveMetadataConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:RemoveMetadataConfiguration>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:RemoveMetadataConfiguration>",
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_SetMetadataConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_SetMetadataConfiguration_REQ * p_req = (trt_SetMetadataConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:SetMetadataConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:Configuration token=\"%s\" "
            "CompressionType=\"%s\" "
            "GeoLocation=\"%s\" "
            "ShapePolygon=\"%s\">", 
        p_req->Configuration.token, 
        p_req->Configuration.CompressionType,
        p_req->Configuration.GeoLocation ? "true" : "false",
        p_req->Configuration.ShapePolygon ? "true" : "false");
    offset += build_MetadataConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:Configuration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:ForcePersistence>%s</trt:ForcePersistence>", 
        p_req->ForcePersistence ? "true" : "false");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:SetMetadataConfiguration>");
    
    return offset;
}

int build_trt_GetMetadataConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetMetadataConfigurationOptions_REQ * p_req = (trt_GetMetadataConfigurationOptions_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetMetadataConfigurationOptions>");

    if (p_req && p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ConfigurationToken>%s</trt:ConfigurationToken>",
            p_req->ConfigurationToken);
    }

    if (p_req && p_req->ProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trt:ProfileToken>%s</trt:ProfileToken>",
            p_req->ProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trt:GetMetadataConfigurationOptions>");
    
    return offset;
}

int build_trt_GetCompatibleVideoEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetCompatibleVideoEncoderConfigurations_REQ * p_req = (trt_GetCompatibleVideoEncoderConfigurations_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetCompatibleVideoEncoderConfigurations>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetCompatibleVideoEncoderConfigurations>",
        p_req->ProfileToken);
    
    return offset;
}

int build_trt_GetCompatibleAudioEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetCompatibleAudioEncoderConfigurations_REQ * p_req = (trt_GetCompatibleAudioEncoderConfigurations_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetCompatibleAudioEncoderConfigurations>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetCompatibleAudioEncoderConfigurations>",
        p_req->ProfileToken);

    return offset;
}

int build_trt_GetCompatibleVideoAnalyticsConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetCompatibleVideoAnalyticsConfigurations_REQ * p_req = (trt_GetCompatibleVideoAnalyticsConfigurations_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetCompatibleVideoAnalyticsConfigurations>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetCompatibleVideoAnalyticsConfigurations>",
        p_req->ProfileToken);

    return offset;        
}

int build_trt_GetCompatibleMetadataConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trt_GetCompatibleMetadataConfigurations_REQ * p_req = (trt_GetCompatibleMetadataConfigurations_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trt:GetCompatibleMetadataConfigurations>"
            "<trt:ProfileToken>%s</trt:ProfileToken>"
        "</trt:GetCompatibleMetadataConfigurations>",
        p_req->ProfileToken);

    return offset;        
}

/***************************************************************************************/

int build_PTZSpeed_xml(char * p_buf, int mlen, onvif_PTZSpeed * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32];

    if (p_req->PanTiltFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:PanTilt x=\"%s\" y=\"%s\"",
            onvif_format_float_num(p_req->PanTilt.x, 6, buff1, sizeof(buff1)),
            onvif_format_float_num(p_req->PanTilt.y, 6, buff2, sizeof(buff2)));

        if (p_req->PanTilt.spaceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " space=\"%s\"",
                p_req->PanTilt.space);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "></tt:PanTilt>");
    }
    
    if (p_req->ZoomFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Zoom x=\"%s\"", 
            onvif_format_float_num(p_req->Zoom.x, 6, buff1, sizeof(buff1)));

        if (p_req->Zoom.spaceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " space=\"%s\"",
                p_req->Zoom.space);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "></tt:Zoom>");    
    }

    return offset;
}

int build_PTZVector_xml(char * p_buf, int mlen, onvif_PTZVector * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32];

    if (p_req->PanTiltFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:PanTilt x=\"%s\" y=\"%s\"", 
            onvif_format_float_num(p_req->PanTilt.x, 6, buff1, sizeof(buff1)),
            onvif_format_float_num(p_req->PanTilt.y, 6, buff2, sizeof(buff2)));

        if (p_req->PanTilt.spaceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " space=\"%s\"",
                p_req->PanTilt.space);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "></tt:PanTilt>");    
    }

    if (p_req->ZoomFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Zoom x=\"%s\"", 
            onvif_format_float_num(p_req->Zoom.x, 6, buff1, sizeof(buff1)));

        if (p_req->Zoom.spaceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " space=\"%s\"",
                p_req->Zoom.space);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "></tt:Zoom>");    
    }
    
    return offset;
}

int build_PTZConfigurationExtension_xml(char * p_buf, int mlen, onvif_PTZConfigurationExtension * p_req)
{
    int offset = 0;
    
    if (p_req->PTControlDirectionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:PTControlDirection>");

        if (p_req->PTControlDirection.EFlipFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:EFlip>"
                    "<tt:Mode>%s</tt:Mode>"
                "</tt:EFlip>",
                onvif_EFlipModeToString(p_req->PTControlDirection.EFlip));                
        }
        
        if (p_req->PTControlDirection.ReverseFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Reverse>"
                    "<tt:Mode>%s</tt:Mode>"
                "</tt:Reverse>",
                onvif_ReverseModeToString(p_req->PTControlDirection.Reverse));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tt:PTControlDirection>");
    }  

    return offset;
}

int build_PTZConfiguration_xml(char * p_buf, int mlen, onvif_PTZConfiguration * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32], buff3[32], buff4[32];
    
    offset += snprintf(p_buf+offset, mlen-offset,
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:NodeToken>%s</tt:NodeToken>",         
        p_req->Name, 
        p_req->UseCount,
        p_req->NodeToken);

   offset += snprintf(p_buf+offset, mlen-offset,      
        "<tt:DefaultAbsolutePantTiltPositionSpace>"
            "http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace"
        "</tt:DefaultAbsolutePantTiltPositionSpace>"
        "<tt:DefaultAbsoluteZoomPositionSpace>"
            "http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace"
        "</tt:DefaultAbsoluteZoomPositionSpace>"
        "<tt:DefaultRelativePanTiltTranslationSpace>"
            "http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace"
        "</tt:DefaultRelativePanTiltTranslationSpace>"
        "<tt:DefaultRelativeZoomTranslationSpace>"
            "http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace"
        "</tt:DefaultRelativeZoomTranslationSpace>"
        "<tt:DefaultContinuousPanTiltVelocitySpace>"
                "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace"
        "</tt:DefaultContinuousPanTiltVelocitySpace>"
        "<tt:DefaultContinuousZoomVelocitySpace>"
            "http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace"
        "</tt:DefaultContinuousZoomVelocitySpace>");

    if (p_req->DefaultPTZSpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:DefaultPTZSpeed>");         
        offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->DefaultPTZSpeed);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:DefaultPTZSpeed>"); 
    }

    if (p_req->DefaultPTZTimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,           
            "<tt:DefaultPTZTimeout>PT%dS</tt:DefaultPTZTimeout>", 
            p_req->DefaultPTZTimeout);
    }    

    if (p_req->PanTiltLimitsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,           
            "<tt:PanTiltLimits>"
                "<tt:Range>"
                    "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
                    "<tt:XRange>"
                        "<tt:Min>%s</tt:Min>"
                        "<tt:Max>%s</tt:Max>"
                    "</tt:XRange>"
                    "<tt:YRange>"
                        "<tt:Min>%s</tt:Min>"
                        "<tt:Max>%s</tt:Max>"
                    "</tt:YRange>"
                "</tt:Range>"
            "</tt:PanTiltLimits>",
            onvif_format_float_num(p_req->PanTiltLimits.XRange.Min, 6, buff1, sizeof(buff1)-1),
            onvif_format_float_num(p_req->PanTiltLimits.XRange.Max, 6, buff2, sizeof(buff2)-1),
            onvif_format_float_num(p_req->PanTiltLimits.YRange.Min, 6, buff3, sizeof(buff3)-1),
            onvif_format_float_num(p_req->PanTiltLimits.YRange.Max, 6, buff4, sizeof(buff4)-1));
    }

    if (p_req->ZoomLimitsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset,           
            "<tt:ZoomLimits>"
                "<tt:Range>"
                    "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI>"
                    "<tt:XRange>"
                        "<tt:Min>%s</tt:Min>"
                        "<tt:Max>%s</tt:Max>"
                    "</tt:XRange>"
                "</tt:Range>"
            "</tt:ZoomLimits>",
            onvif_format_float_num(p_req->ZoomLimits.XRange.Min, 6, buff1, sizeof(buff1)-1),
            onvif_format_float_num(p_req->ZoomLimits.XRange.Max, 6, buff2, sizeof(buff2)-1));
    }

    if (p_req->ExtensionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Extension>");
        offset += build_PTZConfigurationExtension_xml(p_buf+offset, mlen-offset, &p_req->Extension);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Extension>");
    }

    return offset;
}

int build_PTZPresetTourSpot_xml(char * p_buf, int mlen, onvif_PTZPresetTourSpot * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:PresetDetail>");

    if (p_req->PresetDetail.PresetTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:PresetToken>%s</tt:PresetToken>", 
            p_req->PresetDetail.PresetToken);
    }

    if (p_req->PresetDetail.HomeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Home>%s</tt:Home>", 
            p_req->PresetDetail.Home ? "true" : "false");
    }

    if (p_req->PresetDetail.PTZPositionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:PTZPosition>");
        offset += build_PTZVector_xml(p_buf+offset, mlen-offset, &p_req->PresetDetail.PTZPosition);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:PTZPosition>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:PresetDetail>");

    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Speed>");
        offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Speed>");
    }

    if (p_req->StayTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:StayTime>PT%dS</tt:StayTime>", 
            p_req->StayTime);
    }

    return offset;
}

int build_PTZPresetTourStatus_xml(char * p_buf, int mlen, onvif_PTZPresetTourStatus * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tt:Status>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:State>%s</tt:State>", 
        onvif_PTZPresetTourStateToString(p_req->State));

    if (p_req->CurrentTourSpotFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:CurrentTourSpot>");
        offset += build_PTZPresetTourSpot_xml(p_buf+offset, mlen-offset, &p_req->CurrentTourSpot);        
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:CurrentTourSpot>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:Status>");
    
    return offset;
}

int build_PresetTour_xml(char * p_buf, int mlen, onvif_PresetTour * p_req)
{
    int offset = 0;
    PTZPresetTourSpotList * p_TourSpot = p_req->TourSpot;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>", 
        p_req->Name);

    offset += build_PTZPresetTourStatus_xml(p_buf+offset, mlen-offset, &p_req->Status);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:AutoStart>%s</tt:AutoStart>", 
        p_req->AutoStart ? "true" : "false");
    
    if (p_req->StartingCondition.RandomPresetOrderFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:StartingCondition RandomPresetOrder=\"%s\">", 
            p_req->StartingCondition.RandomPresetOrder ? "true" : "false");
    }
    else
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:StartingCondition>");
    }

    if (p_req->StartingCondition.RecurringTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RecurringTime>%d</tt:RecurringTime>", 
            p_req->StartingCondition.RecurringTime);
    }

    if (p_req->StartingCondition.RecurringDurationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RecurringDuration>PT%dS</tt:RecurringDuration>", 
            p_req->StartingCondition.RecurringDuration);
    }

    if (p_req->StartingCondition.DirectionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Direction>%s</tt:Direction>", 
            onvif_PTZPresetTourDirectionToString(p_req->StartingCondition.Direction));
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tt:StartingCondition>");

    while (p_TourSpot)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:TourSpot>");
        offset += build_PTZPresetTourSpot_xml(p_buf+offset, mlen-offset, &p_TourSpot->PTZPresetTourSpot);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:TourSpot>");
        
        p_TourSpot = p_TourSpot->next;
    }

    return offset;
}

int build_ptz_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GetServiceCapabilities />");
    return offset;
}

int build_ptz_GetNodes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GetNodes />");
    return offset;
}

int build_ptz_GetNode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetNode_REQ * p_req = (ptz_GetNode_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetNode>"
            "<tptz:NodeToken>%s</tptz:NodeToken>"
        "</tptz:GetNode>", 
        p_req->NodeToken);

    return offset;
}

int build_ptz_GetPresets_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetPresets_REQ * p_req = (ptz_GetPresets_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetPresets>"
            "<tptz:ProfileToken>%s""</tptz:ProfileToken>"
        "</tptz:GetPresets>", 
        p_req->ProfileToken);

    return offset;
}

int build_ptz_SetPreset_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_SetPreset_REQ * p_req = (ptz_SetPreset_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:SetPreset>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
        p_req->ProfileToken);

    if (p_req->PresetNameFlag && p_req->PresetName[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:PresetName>%s</tptz:PresetName>", 
            p_req->PresetName);
    }     

    if (p_req->PresetTokenFlag && p_req->PresetToken[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:PresetToken>%s</tptz:PresetToken>", 
            p_req->PresetToken);
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:SetPreset>");
    
    return offset;
}

int build_ptz_RemovePreset_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_RemovePreset_REQ * p_req = (ptz_RemovePreset_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:RemovePreset>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:PresetToken>%s</tptz:PresetToken>"
        "</tptz:RemovePreset>", 
        p_req->ProfileToken, 
        p_req->PresetToken);
    
    return offset;
}

int build_ptz_GotoPreset_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GotoPreset_REQ * p_req = (ptz_GotoPreset_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GotoPreset>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>"
        "<tptz:PresetToken>%s</tptz:PresetToken>", 
        p_req->ProfileToken, 
        p_req->PresetToken);

    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Speed>");
        offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);        
        offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Speed>");
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:GotoPreset>");
    
    return offset;
}

int build_ptz_GotoHomePosition_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GotoHomePosition_REQ * p_req = (ptz_GotoHomePosition_REQ *) argv;    
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GotoHomePosition>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>",
        p_req->ProfileToken);
        
    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Speed>");
        offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);        
        offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Speed>");
    }
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:GotoHomePosition>");    
    
    return offset;
}

int build_ptz_SetHomePosition_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_SetHomePosition_REQ * p_req = (ptz_SetHomePosition_REQ *) argv;    
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:SetHomePosition>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
        "</tptz:SetHomePosition>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_ptz_GetStatus_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetStatus_REQ * p_req = (ptz_GetStatus_REQ *) argv;    
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetStatus>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
        "</tptz:GetStatus>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_ptz_ContinuousMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_ContinuousMove_REQ * p_req = (ptz_ContinuousMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:ContinuousMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
        p_req->ProfileToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Velocity>");
       offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Velocity);
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Velocity>");
    
    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:Timeout>PT%dS</tptz:Timeout>", 
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:ContinuousMove>");
    
    return offset;
}

int build_ptz_RelativeMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_RelativeMove_REQ * p_req = (ptz_RelativeMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:RelativeMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
        p_req->ProfileToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Translation>");
       offset += build_PTZVector_xml(p_buf+offset, mlen-offset, &p_req->Translation);
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Translation>");

    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Speed>");
           offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);
        offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Speed>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:RelativeMove>");
    
    return offset;
}
        
int build_ptz_AbsoluteMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_AbsoluteMove_REQ * p_req = (ptz_AbsoluteMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:AbsoluteMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
        p_req->ProfileToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Position>");
       offset += build_PTZVector_xml(p_buf+offset, mlen-offset, &p_req->Position);
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Position>");

    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Speed>");
           offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);
        offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Speed>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:AbsoluteMove>");
    
    return offset;
}

int build_ptz_Stop_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_Stop_REQ * p_req = (ptz_Stop_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Stop>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
        p_req->ProfileToken);

    if (p_req->PanTiltFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:PanTilt>%s</tptz:PanTilt>", 
            p_req->PanTilt ? "true" : "false");
    }

    if (p_req->ZoomFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:Zoom>%s</tptz:Zoom>", 
            p_req->Zoom ? "true" : "false");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Stop>");
    
    return offset;
}

int build_ptz_GetConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GetConfigurations />");
    return offset;
}

int build_ptz_GetConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetConfiguration_REQ * p_req = (ptz_GetConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetConfiguration>"
            "<tptz:PTZConfigurationToken>%s</tptz:PTZConfigurationToken>"
        "</tptz:GetConfiguration>", 
        p_req->ConfigurationToken);
    
    return offset;
}

int build_ptz_SetConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_SetConfiguration_REQ * p_req = (ptz_SetConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:SetConfiguration>");    

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:PTZConfiguration token=\"%s\" "
            "MoveRamp=\"%d\" "
            "PresetRamp=\"%d\" "
            "PresetTourRamp=\"%d\">",
        p_req->PTZConfiguration.token, 
        p_req->PTZConfiguration.MoveRamp, 
        p_req->PTZConfiguration.PresetTourRamp, 
        p_req->PTZConfiguration.PresetTourRamp);
    offset += build_PTZConfiguration_xml(p_buf+offset, mlen-offset, &p_req->PTZConfiguration);    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tptz:PTZConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ForcePersistence>%s</tptz:ForcePersistence>", 
        p_req->ForcePersistence ? "true" : "false");

    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:SetConfiguration>");
    
    return offset;
}

int build_ptz_GetConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetConfigurationOptions_REQ * p_req = (ptz_GetConfigurationOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetConfigurationOptions>"
            "<tptz:ConfigurationToken>%s</tptz:ConfigurationToken>"
        "</tptz:GetConfigurationOptions>", 
        p_req->ConfigurationToken);
    
    return offset;
}

int build_ptz_GetPresetTours_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetPresetTours_REQ * p_req = (ptz_GetPresetTours_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetPresetTours>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
           "</tptz:GetPresetTours>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_ptz_GetPresetTour_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetPresetTour_REQ * p_req = (ptz_GetPresetTour_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetPresetTour>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:PresetTourToken>%s</tptz:PresetTourToken>"
           "</tptz:GetPresetTour>", 
        p_req->ProfileToken, 
        p_req->PresetTourToken);
    
    return offset;
}

int build_ptz_GetPresetTourOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_GetPresetTourOptions_REQ * p_req = (ptz_GetPresetTourOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:GetPresetTourOptions>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:PresetTourToken>%s</tptz:PresetTourToken>"
           "</tptz:GetPresetTourOptions>", 
        p_req->ProfileToken, 
        p_req->PresetTourToken);
    
    return offset;
}

int build_ptz_CreatePresetTour_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_CreatePresetTour_REQ * p_req = (ptz_CreatePresetTour_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:CreatePresetTour>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
           "</tptz:CreatePresetTour>", 
        p_req->ProfileToken);
    
    return offset;
}

int build_ptz_ModifyPresetTour_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_ModifyPresetTour_REQ * p_req = (ptz_ModifyPresetTour_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:ModifyPresetTour>");

       offset += snprintf(p_buf+offset, mlen-offset, 
           "<tptz:ProfileToken>%s</tptz:ProfileToken>", 
           p_req->ProfileToken);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:PresetTour token=\"%s\">", 
        p_req->PresetTour.token);
    offset += build_PresetTour_xml(p_buf+offset, mlen-offset, &p_req->PresetTour);
       offset += snprintf(p_buf+offset, mlen-offset, 
           "</tptz:PresetTour>");

       offset += snprintf(p_buf+offset, mlen-offset, "</tptz:ModifyPresetTour>");    
    
    return offset;
}

int build_ptz_OperatePresetTour_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_OperatePresetTour_REQ * p_req = (ptz_OperatePresetTour_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:OperatePresetTour>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:PresetTourToken>%s</tptz:PresetTourToken>"
            "<tptz:Operation>%s</tptz:Operation>"
           "</tptz:OperatePresetTour>", 
        p_req->ProfileToken, 
        p_req->PresetTourToken,
        onvif_PTZPresetTourOperationToString(p_req->Operation));
    
    return offset;
}

int build_ptz_RemovePresetTour_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_RemovePresetTour_REQ * p_req = (ptz_RemovePresetTour_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:RemovePresetTour>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:PresetTourToken>%s</tptz:PresetTourToken>"
           "</tptz:RemovePresetTour>", 
        p_req->ProfileToken, 
        p_req->PresetTourToken);
    
    return offset;
}

int build_ptz_SendAuxiliaryCommand_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ptz_SendAuxiliaryCommand_REQ * p_req = (ptz_SendAuxiliaryCommand_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:SendAuxiliaryCommand>"
            "<tptz:ProfileToken>%s</tptz:ProfileToken>"
            "<tptz:AuxiliaryData>%s</tptz:AuxiliaryData>"
           "</tptz:SendAuxiliaryCommand>", 
        p_req->ProfileToken, 
        p_req->AuxiliaryData);
    
    return offset;
}

int build_ptz_GeoMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    char buff[32];
    ptz_GeoMove_REQ * p_req = (ptz_GeoMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:GeoMove>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tptz:ProfileToken>%s</tptz:ProfileToken>",
        p_req->ProfileToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Target");

    if (p_req->Target.lonFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " lon=\"%s\"", 
            onvif_format_float_num((float)p_req->Target.lon, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->Target.latFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " lat=\"%s\"", 
            onvif_format_float_num((float)p_req->Target.lat, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->Target.elevationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " elevation=\"%s\"", 
            onvif_format_float_num(p_req->Target.elevation, 6, buff, sizeof(buff)-1));
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "></tptz:Target>");

    if (p_req->SpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tptz:Speed>");
           offset += build_PTZSpeed_xml(p_buf+offset, mlen-offset, &p_req->Speed);
        offset += snprintf(p_buf+offset, mlen-offset, "</tptz:Speed>");
    }

    if (p_req->AreaHeightFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:AreaHeight>%s</tptz:AreaHeight>",
            onvif_format_float_num(p_req->AreaHeight, 6, buff, sizeof(buff)-1));
    }

    if (p_req->AreaWidthFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tptz:AreaWidth>%s</tptz:AreaWidth>",
            onvif_format_float_num(p_req->AreaWidth, 6, buff, sizeof(buff)-1));
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tptz:GeoMove>");
    
    return offset;
}


/***************************************************************************************/

int build_EventFilter_xml(char * p_buf, int mlen, onvif_EventFilter * p_filter)
{
    int i;
    int offset = 0;

    for (i = 0; i < p_filter->sizeTopicExpression; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsnt:TopicExpression dialect=\"%s\">%s</wsnt:TopicExpression>",
            p_filter->TopicExpression[i].Dialect, 
            p_filter->TopicExpression[i].Expression);
    }

    for (i = 0; i < p_filter->sizeMessageContent; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<wsnt:MessageContent dialect=\"%s\">%s</wsnt:MessageContent>",
            p_filter->MessageContent[i].Dialect, 
            p_filter->MessageContent[i].Expression);
    }    

    return offset;
}

int build_tev_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:GetServiceCapabilities />");
    return offset;
}

int build_tev_GetEventProperties_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:GetEventProperties />");
    return offset;
}
    
int build_tev_Renew_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tev_Renew_REQ * p_req = (tev_Renew_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<wsnt:Renew>"
            "<wsnt:TerminationTime>PT%dS</wsnt:TerminationTime>"
        "</wsnt:Renew>", 
        p_req ? (p_req->TerminationTime) : 60);
    
    return offset;
}

int build_tev_Unsubscribe_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<wsnt:Unsubscribe />");
    return offset;
}

int build_tev_Subscribe_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tev_Subscribe_REQ * p_req = (tev_Subscribe_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<wsnt:Subscribe>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<wsnt:ConsumerReference>"
            "<wsa:Address>%s</wsa:Address>"
        "</wsnt:ConsumerReference>",
        p_req->ConsumerReference);

    if (p_req->FiltersFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tev:Filter>");
        offset += build_EventFilter_xml(p_buf+offset, mlen-offset, &p_req->Filter);
        offset += snprintf(p_buf+offset, mlen-offset, "</tev:Filter>");
    }
            
    offset += snprintf(p_buf+offset, mlen-offset,
        "<wsnt:InitialTerminationTime>PT%dS</wsnt:InitialTerminationTime>",
        p_req->InitialTerminationTime);    
        
    offset += snprintf(p_buf+offset, mlen-offset, "</wsnt:Subscribe>");
    
    return offset;
}

int build_tev_PauseSubscription_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:PauseSubscription />");
    return offset;
}

int build_tev_ResumeSubscription_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:ResumeSubscription />");
    return offset;
}

int build_tev_CreatePullPointSubscription_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tev_CreatePullPointSubscription_REQ * p_req = (tev_CreatePullPointSubscription_REQ *)argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:CreatePullPointSubscription>");

    if (p_req->FiltersFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tev:Filter>");
        offset += build_EventFilter_xml(p_buf+offset, mlen-offset, &p_req->Filter);
        offset += snprintf(p_buf+offset, mlen-offset, "</tev:Filter>");
    }
            
    offset += snprintf(p_buf+offset, mlen-offset,
        "<tev:InitialTerminationTime>PT%dS</tev:InitialTerminationTime>",
        p_req->InitialTerminationTime);    
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tev:CreatePullPointSubscription>");
    
    return offset;
}

int build_tev_DestroyPullPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:DestroyPullPoint />");
    return offset;
}

int build_tev_PullMessages_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tev_PullMessages_REQ * p_req = (tev_PullMessages_REQ *)argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tev:PullMessages>"
            "<tev:Timeout>PT%dS</tev:Timeout>"
            "<tev:MessageLimit>%d</tev:MessageLimit>"
        "</tev:PullMessages>",
        p_req->Timeout,
        p_req->MessageLimit);
    
    return offset;
}

int build_tev_GetMessages_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tev_GetMessages_REQ * p_req = (tev_GetMessages_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tev:GetMessages>"
            "<tev:MaximumNumber>%d</tev:MaximumNumber>"
        "</tev:GetMessages>", 
        p_req->MaximumNumber);
    
    return offset;
}

int build_tev_Seek_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    char strtime[64];
    tev_Seek_REQ * p_req = (tev_Seek_REQ *)argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tev:Seek>"
            "<tev:UtcTime>%s</tev:UtcTime>"
            "<tev:Reverse>%s</tev:Reverse>"
        "</tev:Seek>",
        onvif_format_datetime_str(p_req->UtcTime, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)),
        p_req->Reverse ? "true" : "false");
    
    return offset;
}

int build_tev_SetSynchronizationPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tev:SetSynchronizationPoint />");
    return offset;
}

/***************************************************************************************/

int build_BacklightCompensation_xml(char * p_buf, int mlen, onvif_BacklightCompensation * p_req)
{
    int offset = 0;
    char buff[32];
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>", 
        onvif_BacklightCompensationModeToString(p_req->Mode));

    if (p_req->LevelFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Level>%s</tt:Level>", 
            onvif_format_float_num(p_req->Level, 6, buff, sizeof(buff)-1));
    }

    return offset;
}

int build_Exposure_xml(char * p_buf, int mlen, onvif_Exposure * p_req)
{
    int offset = 0;
    char buff[32], buff1[32], buff2[32], buff3[32];
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>", 
        onvif_ExposureModeToString(p_req->Mode));

    if (p_req->PriorityFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Priority>%s</tt:Priority>", 
            onvif_ExposurePriorityToString(p_req->Priority));
    }

    if (p_req->WindowFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Window bottom=\"%s\" top=\"%s\" right=\"%s\" left=\"%s\"></tt:Window>", 
            onvif_format_float_num(p_req->Window.bottom, 2, buff, sizeof(buff)-1),
            onvif_format_float_num(p_req->Window.top, 2, buff1, sizeof(buff1)-1),
            onvif_format_float_num(p_req->Window.right, 2, buff2, sizeof(buff2)-1),
            onvif_format_float_num(p_req->Window.left, 2, buff3, sizeof(buff3)-1));
    }
    
    if (p_req->MinExposureTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MinExposureTime>%s</tt:MinExposureTime>", 
            onvif_format_float_num(p_req->MinExposureTime, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->MaxExposureTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MaxExposureTime>%s</tt:MaxExposureTime>", 
            onvif_format_float_num(p_req->MaxExposureTime, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->MinGainFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MinGain>%s</tt:MinGain>", 
            onvif_format_float_num(p_req->MinGain, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->MaxGainFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MaxGain>%s</tt:MaxGain>", 
            onvif_format_float_num(p_req->MaxGain, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->MinIrisFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MinIris>%s</tt:MinIris>", 
            onvif_format_float_num(p_req->MinIris, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->MaxIrisFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MaxIris>%s</tt:MaxIris>", 
            onvif_format_float_num(p_req->MaxIris, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->ExposureTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:ExposureTime>%s</tt:ExposureTime>", 
            onvif_format_float_num(p_req->ExposureTime, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->GainFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Gain>%s</tt:Gain>", 
            onvif_format_float_num(p_req->Gain, 6, buff, sizeof(buff)-1));
    }    
    
    if (p_req->IrisFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Iris>%s</tt:Iris>", 
            onvif_format_float_num(p_req->Iris, 6, buff, sizeof(buff)-1));
    }

    return offset;        
}

int build_FocusConfiguration_xml(char * p_buf, int mlen, onvif_FocusConfiguration * p_req)
{
    int offset = 0;
    char buff[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:AutoFocusMode>%s</tt:AutoFocusMode>", 
        onvif_AutoFocusModeToString(p_req->AutoFocusMode));

    if (p_req->DefaultSpeedFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:DefaultSpeed>%s</tt:DefaultSpeed>", 
            onvif_format_float_num(p_req->DefaultSpeed, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->NearLimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:NearLimit>%s</tt:NearLimit>", 
            onvif_format_float_num(p_req->NearLimit, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->FarLimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:FarLimit>%s</tt:FarLimit>", 
            onvif_format_float_num(p_req->FarLimit, 6, buff, sizeof(buff)-1));
    }

    return offset;
}

int build_WideDynamicRange_xml(char * p_buf, int mlen, onvif_WideDynamicRange * p_req)
{
    int offset = 0;
    char buff[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>", 
        onvif_WideDynamicModeToString(p_req->Mode));

    if (p_req->LevelFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Level>%s</tt:Level>",
            onvif_format_float_num(p_req->Level, 6, buff, sizeof(buff)-1));
    }

    return offset;
}

int build_WhiteBalance_xml(char * p_buf, int mlen, onvif_WhiteBalance * p_req)
{
    int offset = 0;
    char buff[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>", 
        onvif_WhiteBalanceModeToString(p_req->Mode));

    if (p_req->CrGainFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:CrGain>%s</tt:CrGain>", 
            onvif_format_float_num(p_req->CrGain, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->CbGainFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:CbGain>%s</tt:CbGain>", 
            onvif_format_float_num(p_req->CbGain, 6, buff, sizeof(buff)-1));
    }

    return offset;
}

int build_FocusMove_xml(char * p_buf, int mlen, onvif_FocusMove * p_req)
{
    int offset = 0;
    char buff[32];
    
    if (p_req->AbsoluteFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Absolute>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Position>%s</tt:Position>",
            onvif_format_float_num(p_req->Absolute.Position, 2, buff, sizeof(buff)-1));

        if (p_req->Absolute.SpeedFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Speed>%s</tt:Speed>",
                onvif_format_float_num(p_req->Absolute.Speed, 2, buff, sizeof(buff)-1));            
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Absolute>");
    }
    
    if (p_req->RelativeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Relative>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Distance>%s</tt:Distance>",
            onvif_format_float_num(p_req->Relative.Distance, 2, buff, sizeof(buff)-1));

        if (p_req->Relative.SpeedFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:Speed>%s</tt:Speed>",
                onvif_format_float_num(p_req->Relative.Speed, 2, buff, sizeof(buff)-1));            
        }    

        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Relative>");
    }
    
    if (p_req->ContinuousFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Continuous>"
                "<tt:Speed>%s</tt:Speed>"
            "</tt:Continuous>",
            onvif_format_float_num(p_req->Continuous.Speed, 2, buff, sizeof(buff)-1)); 
    }

    return offset;
}

int build_img_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<timg:GetServiceCapabilities />");
    return offset;
}

int build_img_GetImagingSettings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetImagingSettings_REQ * p_req = (img_GetImagingSettings_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetImagingSettings>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetImagingSettings>",
        p_req->VideoSourceToken);
    
    return offset;
}

int build_img_SetImagingSettings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    char buff[32];
    img_SetImagingSettings_REQ * p_req = (img_SetImagingSettings_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<timg:SetImagingSettings>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:VideoSourceToken>%s</timg:VideoSourceToken>", 
        p_req->VideoSourceToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<timg:ImagingSettings>");    

    if (p_req->ImagingSettings.BacklightCompensationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:BacklightCompensation>");
        offset += build_BacklightCompensation_xml(p_buf+offset, mlen-offset, &p_req->ImagingSettings.BacklightCompensation);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:BacklightCompensation>");
    }

    if (p_req->ImagingSettings.BrightnessFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Brightness>%s</tt:Brightness>", 
            onvif_format_float_num(p_req->ImagingSettings.Brightness, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->ImagingSettings.ColorSaturationFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:ColorSaturation>%s</tt:ColorSaturation>", 
            onvif_format_float_num(p_req->ImagingSettings.ColorSaturation, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->ImagingSettings.ContrastFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Contrast>%s</tt:Contrast>", 
            onvif_format_float_num(p_req->ImagingSettings.Contrast, 6, buff, sizeof(buff)-1));
    }

    if (p_req->ImagingSettings.ExposureFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Exposure>");
        offset += build_Exposure_xml(p_buf+offset, mlen-offset, &p_req->ImagingSettings.Exposure);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Exposure>");            
    }

    if (p_req->ImagingSettings.FocusFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Focus>");
        offset += build_FocusConfiguration_xml(p_buf+offset, mlen-offset, &p_req->ImagingSettings.Focus);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Focus>");
    }

    if (p_req->ImagingSettings.IrCutFilterFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:IrCutFilter>%s</tt:IrCutFilter>", 
            onvif_IrCutFilterModeToString(p_req->ImagingSettings.IrCutFilter));
    }

    if (p_req->ImagingSettings.SharpnessFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Sharpness>%s</tt:Sharpness>", 
            onvif_format_float_num(p_req->ImagingSettings.Sharpness, 6, buff, sizeof(buff)-1));
    }

    if (p_req->ImagingSettings.WideDynamicRangeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:WideDynamicRange>");
        offset += build_WideDynamicRange_xml(p_buf+offset, mlen-offset, &p_req->ImagingSettings.WideDynamicRange);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:WideDynamicRange>");    
    }

    if (p_req->ImagingSettings.WhiteBalanceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:WhiteBalance>");
        offset += build_WhiteBalance_xml(p_buf+offset, mlen-offset, &p_req->ImagingSettings.WhiteBalance);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:WhiteBalance>");    
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</timg:ImagingSettings>");

    if (p_req->ForcePersistenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<timg:ForcePersistence>%s</timg:ForcePersistence>", 
            p_req->ForcePersistence ? "true" : "false");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</timg:SetImagingSettings>");
    
    return offset;
}

int build_img_GetOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetOptions_REQ * p_req = (img_GetOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetOptions>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetOptions>", 
        p_req->VideoSourceToken);
    
    return offset;
}

int build_img_Move_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_Move_REQ * p_req = (img_Move_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<timg:Move>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:VideoSourceToken>%s</timg:VideoSourceToken>",
        p_req->VideoSourceToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<timg:Focus>");
    offset += build_FocusMove_xml(p_buf+offset, mlen-offset, &p_req->Focus);
    offset += snprintf(p_buf+offset, mlen-offset, "</timg:Focus>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</timg:Move>");
    
    return offset;
}
  
int build_img_Stop_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_Stop_REQ * p_req = (img_Stop_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:Stop>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:Stop>", 
        p_req->VideoSourceToken);
    
    return offset;
}
 
int build_img_GetStatus_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetStatus_REQ * p_req = (img_GetStatus_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetStatus>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetStatus>", 
        p_req->VideoSourceToken);
    
    return offset;
}
 
int build_img_GetMoveOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetMoveOptions_REQ * p_req = (img_GetMoveOptions_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetMoveOptions>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetMoveOptions>", 
        p_req->VideoSourceToken);
    
    return offset;
}

int build_img_GetPresets_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetPresets_REQ * p_req = (img_GetPresets_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetPresets>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetPresets>",
        p_req->VideoSourceToken);
    
    return offset;
}

int build_img_GetCurrentPreset_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_GetCurrentPreset_REQ * p_req = (img_GetCurrentPreset_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:GetCurrentPreset>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
        "</timg:GetCurrentPreset>",
        p_req->VideoSourceToken);
    
    return offset;
}

int build_img_SetCurrentPreset_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    img_SetCurrentPreset_REQ * p_req = (img_SetCurrentPreset_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<timg:SetCurrentPreset>"
            "<timg:VideoSourceToken>%s</timg:VideoSourceToken>"
            "<timg:PresetToken>%s</timg:PresetToken>"
        "</timg:SetCurrentPreset>",
        p_req->VideoSourceToken,
        p_req->PresetToken);
    
    return offset;
}

/***************************************************************************************/

int build_tan_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tan:GetServiceCapabilities />");
    return offset;
}

int build_tan_GetSupportedRules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetSupportedRules_REQ * p_req = (tan_GetSupportedRules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetSupportedRules>"
            "<tan:ConfigurationToken>%s</tan:ConfigurationToken>"
        "</tan:GetSupportedRules>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_tan_CreateRules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ConfigList * p_config;
    tan_CreateRules_REQ * p_req = (tan_CreateRules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:CreateRules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    p_config = p_req->Rule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:Rule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:Rule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tan:Rule>");
        
        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:CreateRules>");
    
    return offset;
}

int build_tan_DeleteRules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tan_DeleteRules_REQ * p_req = (tan_DeleteRules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:DeleteRules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    for (i = 0; i < p_req->sizeRuleName; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tan:RuleName>%s</tan:RuleName>", 
            p_req->RuleName[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:DeleteRules>");
    
    return offset;
}

int build_tan_GetRules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetRules_REQ * p_req = (tan_GetRules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetRules>"
            "<tan:ConfigurationToken>%s</tan:ConfigurationToken>"
        "</tan:GetRules>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_tan_ModifyRules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ConfigList * p_config;
    tan_ModifyRules_REQ * p_req = (tan_ModifyRules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:ModifyRules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    p_config = p_req->Rule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:Rule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:Rule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tan:Rule>");
        
        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:ModifyRules>");
    
    return offset;
}

int build_tan_CreateAnalyticsModules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ConfigList * p_config;
    tan_CreateAnalyticsModules_REQ * p_req = (tan_CreateAnalyticsModules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:CreateAnalyticsModules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    p_config = p_req->AnalyticsModule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:AnalyticsModule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:AnalyticsModule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tan:AnalyticsModule>");
        
        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:CreateAnalyticsModules>");
    
    return offset;
}

int build_tan_DeleteAnalyticsModules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i, offset = 0;
    tan_DeleteAnalyticsModules_REQ * p_req = (tan_DeleteAnalyticsModules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:DeleteAnalyticsModules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    for (i = 0; i < p_req->sizeAnalyticsModuleName; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tan:AnalyticsModuleName>%s</tan:AnalyticsModuleName>", 
            p_req->AnalyticsModuleName[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:DeleteAnalyticsModules>");
    
    return offset;
}

int build_tan_GetAnalyticsModules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetAnalyticsModules_REQ * p_req = (tan_GetAnalyticsModules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetAnalyticsModules>"
            "<tan:ConfigurationToken>%s</tan:ConfigurationToken>"
        "</tan:GetAnalyticsModules>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_tan_ModifyAnalyticsModules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    ConfigList * p_config;
    tan_ModifyAnalyticsModules_REQ * p_req = (tan_ModifyAnalyticsModules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:ModifyAnalyticsModules>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>", 
        p_req->ConfigurationToken);

    p_config = p_req->AnalyticsModule;
    while (p_config)
    {
        if (p_config->Config.attrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:AnalyticsModule Name=\"%s\" Type=\"%s\" %s>", 
                p_config->Config.Name, 
                p_config->Config.Type, 
                p_config->Config.attr);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tan:AnalyticsModule Name=\"%s\" Type=\"%s\">", 
                p_config->Config.Name, 
                p_config->Config.Type);
        }
        
        offset += build_Config_xml(p_buf+offset, mlen-offset, &p_config->Config);
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "</tan:AnalyticsModule>");
        
        p_config = p_config->next;
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:ModifyAnalyticsModules>");
    
    return offset;
}

int build_tan_GetSupportedAnalyticsModules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetSupportedAnalyticsModules_REQ * p_req = (tan_GetSupportedAnalyticsModules_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetSupportedAnalyticsModules>"
            "<tan:ConfigurationToken>%s</tan:ConfigurationToken>"
        "</tan:GetSupportedAnalyticsModules>",
        p_req->ConfigurationToken);
    
    return offset;
}

int build_tan_GetRuleOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetRuleOptions_REQ * p_req = (tan_GetRuleOptions_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tan:GetRuleOptions>");

    if (p_req->RuleTypeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tan:RuleType>%s</tan:RuleType>",
            p_req->RuleType);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:ConfigurationToken>%s</tan:ConfigurationToken>",
        p_req->ConfigurationToken);
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tan:GetRuleOptions>");
    
    return offset;
}

int build_tan_GetAnalyticsModuleOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetAnalyticsModuleOptions_REQ * p_req = (tan_GetAnalyticsModuleOptions_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetAnalyticsModuleOptions>"
            "<tan:Type>%s</tan:Type>"
            "<tan:ConfigurationToken>%s</tan:ConfigurationToken>"
        "</tan:GetAnalyticsModuleOptions>",
        p_req->Type,
        p_req->ConfigurationToken);
    
    return offset;
}

int build_tan_GetSupportedMetadata_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tan_GetSupportedMetadata_REQ * p_req = (tan_GetSupportedMetadata_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tan:GetSupportedMetadata>"
            "<tan:Type>%s</tan:Type>"
        "</tan:GetSupportedMetadata>",
        p_req->Type);
    
    return offset;
}

/***************************************************************************************/

int build_VideoEncoder2Configuration_xml(char * p_buf, int mlen, onvif_VideoEncoder2Configuration * p_req)
{
    int offset = 0;
    char buff[32];
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:Encoding>%s</tt:Encoding>"
        "<tt:Resolution>"
            "<tt:Width>%d</tt:Width>"
            "<tt:Height>%d</tt:Height>"
        "</tt:Resolution>",
        p_req->Name, 
        p_req->UseCount, 
        p_req->Encoding, 
        p_req->Resolution.Width, 
        p_req->Resolution.Height);

    if (p_req->RateControlFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RateControl");

        if (p_req->RateControl.ConstantBitRateFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " ConstantBitRate=\"%s\"",
                p_req->RateControl.ConstantBitRate ? "true" : "false");
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, ">");
            
        offset += snprintf(p_buf+offset, mlen-offset, 
                "<tt:FrameRateLimit>%s</tt:FrameRateLimit>"
                "<tt:BitrateLimit>%d</tt:BitrateLimit>"
            "</tt:RateControl>",
            onvif_format_float_num(p_req->RateControl.FrameRateLimit, 2, buff, sizeof(buff)-1),
            p_req->RateControl.BitrateLimit);
    }

    if (p_req->MulticastFlag)
    {
        offset += build_MulticastConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Multicast);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Quality>%s</tt:Quality>", 
        onvif_format_float_num(p_req->Quality, 2, buff, sizeof(buff)-1));

    return offset;  
}

int build_AudioEncoder2Configuration_xml(char * p_buf, int mlen, onvif_AudioEncoder2Configuration * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Name>%s</tt:Name>"
        "<tt:UseCount>%d</tt:UseCount>"
        "<tt:Encoding>%s</tt:Encoding>"
        "<tt:Bitrate>%d</tt:Bitrate>"
        "<tt:SampleRate>%d</tt:SampleRate>", 
        p_req->Name, 
        p_req->UseCount, 
        p_req->Encoding, 
        p_req->Bitrate, 
        p_req->SampleRate); 

    if (p_req->MulticastFlag)
    {
        offset += build_MulticastConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Multicast);
    }
        
    return offset;   
}

int build_Polygon_xml(char * p_buf, int mlen, onvif_Polygon * p_req)
{
    uint32 i;
    int offset = 0;
    char buff1[32], buff2[32];

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:Polygon>");

    for (i = 0; i < p_req->sizePoint; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Point x=\"%s\" y=\"%s\" />",
            onvif_format_float_num(p_req->Point[i].x, 2, buff1, sizeof(buff1)),
            onvif_format_float_num(p_req->Point[i].y, 2, buff2, sizeof(buff2)));
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tt:Polygon>");
    
    return offset;
}

int build_Color_xml(char * p_buf, int mlen, onvif_Color * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32], buff3[32];

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Color X=\"%s\" Y=\"%s\" Z=\"%s\"", 
        onvif_format_float_num(p_req->X, 2, buff1, sizeof(buff1)),
        onvif_format_float_num(p_req->Y, 2, buff2, sizeof(buff2)),
        onvif_format_float_num(p_req->Z, 2, buff3, sizeof(buff3)));

    if (p_req->ColorspaceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " Colorspace=\"%s\"", 
            p_req->Colorspace);
    }

    offset += snprintf(p_buf+offset, mlen-offset, " />");

    return offset;
}

int build_Mask_xml(char * p_buf, int mlen, onvif_Mask * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>",
        p_req->ConfigurationToken);

    offset += build_Polygon_xml(p_buf+offset, mlen-offset, &p_req->Polygon);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:Type>%s</tr2:Type>", 
        p_req->Type);

    if (p_req->ColorFlag)
    {
        offset += build_Color_xml(p_buf+offset, mlen-offset, &p_req->Color);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:Enabled>%s</tr2:Enabled>", 
        p_req->Enabled ? "true" : "false");

    return offset;
}

int build_tr2_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetServiceCapabilities />");
    return offset;
}

int build_tr2_GetConfiguration_xml(char * p_buf, int mlen, tr2_GetConfiguration * p_req)
{
    int offset = 0;
    
    if (p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>", 
            p_req->ConfigurationToken);
    }
    
    if (p_req->ProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:ProfileToken>%s</tr2:ProfileToken>", 
            p_req->ProfileToken);
    }

    return offset;
}

int build_tr2_GetVideoEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoEncoderConfigurations_REQ * p_req = (tr2_GetVideoEncoderConfigurations_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetVideoEncoderConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetVideoEncoderConfigurations>");

    return offset;
}

int build_tr2_SetVideoEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetVideoEncoderConfiguration_REQ * p_req = (tr2_SetVideoEncoderConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetVideoEncoderConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:Configuration token=\"%s\"", 
        p_req->Configuration.token);

    if (p_req->Configuration.GovLengthFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " GovLength=\"%d\"", 
            p_req->Configuration.GovLength);
    }

    if (p_req->Configuration.AnchorFrameDistanceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " AnchorFrameDistance=\"%d\"", 
            p_req->Configuration.AnchorFrameDistance);
    }
    
    if (p_req->Configuration.ProfileFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " Profile=\"%s\"", 
            p_req->Configuration.Profile);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        " GuaranteedFrameRate=\"%s\" Signed=\"%s\"", 
        p_req->Configuration.GuaranteedFrameRate ? "true" : "false",
        p_req->Configuration.Signed ? "true" : "false");
    
    offset += snprintf(p_buf+offset, mlen-offset, ">");

    offset += build_VideoEncoder2Configuration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>");        

    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:SetVideoEncoderConfiguration>");
    
    return offset;
}

int build_tr2_GetVideoEncoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoEncoderConfigurationOptions_REQ * p_req = (tr2_GetVideoEncoderConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetVideoEncoderConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetVideoEncoderConfigurationOptions>");

    return offset;
}

int build_tr2_GetProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tr2_GetProfiles_REQ * p_req = (tr2_GetProfiles_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetProfiles>");

    if (p_req)
    {
        if (p_req->TokenFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tr2:Token>%s</tr2:Token>",
                p_req->Token);
        }

        for (i = 0; i < p_req->sizeType; i++)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tr2:Type>%s</tr2:Type>",
                p_req->Type[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetProfiles>");

    return offset;
}

int build_tr2_ConfigurationRef(char * p_buf, int mlen, onvif_ConfigurationRef * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:Type>%s</tr2:Type>",
        p_req->Type);

    if (p_req->TokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:Token>%s</tr2:Token>",
            p_req->Token);
    }

    return offset;
}

int build_tr2_CreateProfile_xml    (char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tr2_CreateProfile_REQ * p_req = (tr2_CreateProfile_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:CreateProfile>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:Name>%s</tr2:Name>", 
        p_req->Name);

    for (i = 0; i < p_req->sizeConfiguration; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tr2:Configuration>");
        offset += build_tr2_ConfigurationRef(p_buf+offset, mlen-offset, &p_req->Configuration[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tr2:Configuration>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:CreateProfile>");

    return offset;
}

int build_tr2_DeleteProfile_xml    (char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_DeleteProfile_REQ * p_req = (tr2_DeleteProfile_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:DeleteProfile>"    
            "<tr2:Token>%s</tr2:Token>"
        "</tr2:DeleteProfile>",
        p_req->Token);

    return offset;
}

int build_tr2_GetStreamUri_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetStreamUri_REQ * p_req = (tr2_GetStreamUri_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetStreamUri>"
            "<tr2:Protocol>%s</tr2:Protocol>"
            "<tr2:ProfileToken>%s</tr2:ProfileToken>"
        "</tr2:GetStreamUri>",
        p_req->Protocol,
        p_req->ProfileToken);

    return offset;
}

int build_tr2_GetVideoSourceConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoSourceConfigurations_REQ * p_req = (tr2_GetVideoSourceConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetVideoSourceConfigurations>");
    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetVideoSourceConfigurations>");

    return offset;
}

int build_tr2_GetVideoSourceConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoSourceConfigurationOptions_REQ * p_req = (tr2_GetVideoSourceConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetVideoSourceConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetVideoSourceConfigurationOptions>");

    return offset;
}

int build_tr2_SetVideoSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetVideoSourceConfiguration_REQ * p_req = (tr2_SetVideoSourceConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetVideoSourceConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);
    offset += build_VideoSourceConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetVideoSourceConfiguration>");

    return offset;
}

int build_tr2_SetSynchronizationPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetSynchronizationPoint_REQ * p_req = (tr2_SetSynchronizationPoint_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetSynchronizationPoint>"
            "<tr2:ProfileToken>%s</tr2:ProfileToken>"
        "</tr2:SetSynchronizationPoint>",
        p_req->ProfileToken);

    return offset;
}
    
int build_tr2_GetMetadataConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetMetadataConfigurations_REQ * p_req = (tr2_GetMetadataConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetMetadataConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetMetadataConfigurations>");

    return offset;
}
    
int build_tr2_GetMetadataConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetMetadataConfigurationOptions_REQ * p_req = (tr2_GetMetadataConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetMetadataConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetMetadataConfigurationOptions>");

    return offset;
}
    
int build_tr2_SetMetadataConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetMetadataConfiguration_REQ * p_req = (tr2_SetMetadataConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetMetadataConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);
    offset += build_MetadataConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetMetadataConfiguration>");

    return offset;
}
    
int build_tr2_GetAudioEncoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioEncoderConfigurations_REQ * p_req = (tr2_GetAudioEncoderConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioEncoderConfigurations>");
    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioEncoderConfigurations>");

    return offset;
}
    
int build_tr2_GetAudioSourceConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioSourceConfigurations_REQ * p_req = (tr2_GetAudioSourceConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioSourceConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioSourceConfigurations>");

    return offset;
}
    
int build_tr2_GetAudioSourceConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioSourceConfigurationOptions_REQ * p_req = (tr2_GetAudioSourceConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioSourceConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioSourceConfigurationOptions>");

    return offset;
}
    
int build_tr2_SetAudioSourceConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetAudioSourceConfiguration_REQ * p_req = (tr2_SetAudioSourceConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetAudioSourceConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);    
    offset += build_AudioSourceConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetAudioSourceConfiguration>");

    return offset;
}

int build_tr2_SetAudioEncoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetAudioEncoderConfiguration_REQ * p_req = (tr2_SetAudioEncoderConfiguration_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetAudioEncoderConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);
    offset += build_AudioEncoder2Configuration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetAudioEncoderConfiguration>");

    return offset;
}
    
int build_tr2_GetAudioEncoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioEncoderConfigurationOptions_REQ * p_req = (tr2_GetAudioEncoderConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioEncoderConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioEncoderConfigurationOptions>");

    return offset;
}    
    
int build_tr2_AddConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tr2_AddConfiguration_REQ * p_req = (tr2_AddConfiguration_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:AddConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:ProfileToken>%s</tr2:ProfileToken>",
        p_req->ProfileToken);

    if (p_req->NameFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:Name>%s</tr2:Name>",
            p_req->Name);
    }

    for (i = 0; i < p_req->sizeConfiguration; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tr2:Configuration>");
        offset += build_tr2_ConfigurationRef(p_buf+offset, mlen-offset, &p_req->Configuration[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tr2:Configuration>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:AddConfiguration>");

    return offset;
}
    
int build_tr2_RemoveConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tr2_RemoveConfiguration_REQ * p_req = (tr2_RemoveConfiguration_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:RemoveConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:ProfileToken>%s</tr2:ProfileToken>",
        p_req->ProfileToken);

    for (i = 0; i < p_req->sizeConfiguration; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tr2:Configuration>");
        offset += build_tr2_ConfigurationRef(p_buf+offset, mlen-offset, &p_req->Configuration[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tr2:Configuration>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:RemoveConfiguration>");

    return offset;
}
    
int build_tr2_GetVideoEncoderInstances_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoEncoderInstances_REQ * p_req = (tr2_GetVideoEncoderInstances_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetVideoEncoderInstances>"
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>"
        "</tr2:GetVideoEncoderInstances>",
        p_req->ConfigurationToken);

    return offset;
}    

int build_tr2_GetAudioOutputConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioOutputConfigurations_REQ * p_req = (tr2_GetAudioOutputConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioOutputConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioOutputConfigurations>");

    return offset;
}

int build_tr2_GetAudioOutputConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioOutputConfigurationOptions_REQ * p_req = (tr2_GetAudioOutputConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioOutputConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioOutputConfigurationOptions>");

    return offset;
}

int build_tr2_SetAudioOutputConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetAudioOutputConfiguration_REQ * p_req = (tr2_SetAudioOutputConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetAudioOutputConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);
    offset += build_AudioOutputConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetAudioOutputConfiguration>");

    return offset;
}

int build_tr2_GetAudioDecoderConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioDecoderConfigurations_REQ * p_req = (tr2_GetAudioDecoderConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioDecoderConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioDecoderConfigurations>");

    return offset;
}

int build_tr2_GetAudioDecoderConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAudioDecoderConfigurationOptions_REQ * p_req = (tr2_GetAudioDecoderConfigurationOptions_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAudioDecoderConfigurationOptions>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAudioDecoderConfigurationOptions>");

    return offset;
}

int build_tr2_SetAudioDecoderConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetAudioDecoderConfiguration_REQ * p_req = (tr2_SetAudioDecoderConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetAudioDecoderConfiguration>"
        "<tr2:Configuration token=\"%s\">",
        p_req->Configuration.token);
    offset += build_AudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tr2:Configuration>"
        "</tr2:SetAudioDecoderConfiguration>");

    return offset;
}

int build_tr2_GetSnapshotUri_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetSnapshotUri_REQ * p_req = (tr2_GetSnapshotUri_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetSnapshotUri>"
            "<tr2:ProfileToken>%s</tr2:ProfileToken>"
        "</tr2:GetSnapshotUri>",
        p_req->ProfileToken);

    return offset;
}

int build_tr2_StartMulticastStreaming_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_StartMulticastStreaming_REQ * p_req = (tr2_StartMulticastStreaming_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:StartMulticastStreaming>"
            "<tr2:ProfileToken>%s</tr2:ProfileToken>"
        "</tr2:StartMulticastStreaming>",
        p_req->ProfileToken);

    return offset;
}

int build_tr2_StopMulticastStreaming_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_StopMulticastStreaming_REQ * p_req = (tr2_StopMulticastStreaming_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:StopMulticastStreaming>"
            "<tr2:ProfileToken>%s</tr2:ProfileToken>"
        "</tr2:StopMulticastStreaming>",
        p_req->ProfileToken);

    return offset;
}

int build_tr2_GetVideoSourceModes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetVideoSourceModes_REQ * p_req = (tr2_GetVideoSourceModes_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetVideoSourceModes>"
            "<tr2:VideoSourceToken>%s</tr2:VideoSourceToken>"
        "</tr2:GetVideoSourceModes>",
        p_req->VideoSourceToken);

    return offset;
}

int build_tr2_SetVideoSourceMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetVideoSourceMode_REQ * p_req = (tr2_SetVideoSourceMode_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:SetVideoSourceMode>"
            "<tr2:VideoSourceToken>%s</tr2:VideoSourceToken>"
            "<tr2:VideoSourceModeToken>%s</tr2:VideoSourceModeToken>"
        "</tr2:SetVideoSourceMode>",
        p_req->VideoSourceToken,
        p_req->VideoSourceModeToken);

    return offset;
}

int build_tr2_CreateOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_CreateOSD_REQ * p_req = (tr2_CreateOSD_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:CreateOSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:OSD token=\"%s\">", p_req->OSD.token);
    offset += build_OSDConfiguration_xml(p_buf+offset, mlen-offset, &p_req->OSD);
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:OSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:CreateOSD>");

    return offset;
}

int build_tr2_DeleteOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_DeleteOSD_REQ * p_req = (tr2_DeleteOSD_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:DeleteOSD>"
            "<tr2:OSDToken>%s</tr2:OSDToken>"
        "</tr2:DeleteOSD>",
        p_req->OSDToken);

    return offset;
}

int build_tr2_GetOSDs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetOSDs_REQ * p_req = (tr2_GetOSDs_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetOSDs>");

    if (p_req && p_req->OSDTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:OSDToken>%s</tr2:OSDToken>",
            p_req->OSDToken);
    }

    if (p_req && p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>",
            p_req->ConfigurationToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetOSDs>");

    return offset;
}

int build_tr2_SetOSD_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetOSD_REQ * p_req = (tr2_SetOSD_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:SetOSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:OSD token=\"%s\">", p_req->OSD.token);
    offset += build_OSDConfiguration_xml(p_buf+offset, mlen-offset, &p_req->OSD);
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:OSD>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:SetOSD>");

    return offset;
}

int build_tr2_GetOSDOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetOSDOptions_REQ * p_req = (tr2_GetOSDOptions_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetOSDOptions>"
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>"
        "</tr2:GetOSDOptions>",
        p_req->ConfigurationToken);

    return offset;
}

int build_tr2_GetAnalyticsConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetAnalyticsConfigurations_REQ * p_req = (tr2_GetAnalyticsConfigurations_REQ *) argv;
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetAnalyticsConfigurations>");

    if (p_req)
    {
        offset += build_tr2_GetConfiguration_xml(p_buf+offset, mlen-offset, &p_req->GetConfiguration);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetAnalyticsConfigurations>");

    return offset;
}



int build_tr2_GetMasks_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetMasks_REQ * p_req = (tr2_GetMasks_REQ *) argv;
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:GetMasks>");

    if (p_req && p_req->TokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:Token>%s</tr2:Token>",
            p_req->Token);
    }

    if (p_req && p_req->ConfigurationTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>",
            p_req->ConfigurationToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:GetMasks>");

    return offset;
}

int build_tr2_SetMask_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_SetMask_REQ * p_req = (tr2_SetMask_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:SetMask>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:Mask token=\"%s\">", p_req->Mask.token);
    offset += build_Mask_xml(p_buf+offset, mlen-offset, &p_req->Mask);
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:Mask>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:SetMask>");

    return offset;
}

int build_tr2_CreateMask_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_CreateMask_REQ * p_req = (tr2_CreateMask_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:CreateMask>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tr2:Mask token=\"%s\">", p_req->Mask.token);
    offset += build_Mask_xml(p_buf+offset, mlen-offset, &p_req->Mask);
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:Mask>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tr2:CreateMask>");

    return offset;
}

int build_tr2_DeleteMask_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_DeleteMask_REQ * p_req = (tr2_DeleteMask_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:DeleteMask>"
            "<tr2:Token>%s</tr2:Token>"
        "</tr2:DeleteMask>",
        p_req->Token);

    return offset;
}

int build_tr2_GetMaskOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tr2_GetMaskOptions_REQ * p_req = (tr2_GetMaskOptions_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tr2:GetMaskOptions>"
            "<tr2:ConfigurationToken>%s</tr2:ConfigurationToken>"
        "</tr2:GetMaskOptions>",
        p_req->ConfigurationToken);

    return offset;
}

#ifdef DEVICEIO_SUPPORT

int build_RelayOut_xml(char * p_buf, int mlen, onvif_RelayOutput * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Properties>"
            "<tt:Mode>%s</tt:Mode>"
            "<tt:DelayTime>PT%dS</tt:DelayTime>"
            "<tt:IdleState>%s</tt:IdleState>"
        "</tt:Properties>",
        onvif_RelayModeToString(p_req->Properties.Mode),
        p_req->Properties.DelayTime,
        onvif_RelayIdleStateToString(p_req->Properties.IdleState));

    return offset;
}

int build_SerialPortConfiguration_xml(char * p_buf, int mlen, onvif_SerialPortConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:BaudRate>%d</tmd:BaudRate>\r\n"
        "<tmd:ParityBit>%s</tmd:ParityBit>\r\n"
        "<tmd:CharacterLength>%d</tmd:CharacterLength>\r\n"
        "<tmd:StopBit>%0.1f</tmd:StopBit>\r\n",
        p_req->BaudRate,
        onvif_ParityBitToString(p_req->ParityBit),
        p_req->CharacterLength,
        p_req->StopBit);

    return offset;
}

int build_SendReceiveSerialCommand_xml(char * p_buf, int mlen, onvif_SendReceiveSerialCommand * p_req)
{
    int offset = 0;
    
    if (p_req->SerialDataFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tmd:SerialData>");

        if (p_req->SerialData._union_SerialData == 0)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tmd:Binary>%s</tmd:Binary>",
                p_req->SerialData.union_SerialData.Binary);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tmd:String>%s</tmd:String>",
                p_req->SerialData.union_SerialData.String);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tmd:SerialData>");
    }

    if (p_req->TimeOutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tmd:TimeOut>PT%uS</tmd:TimeOut>",
            p_req->TimeOut);
    }

    if (p_req->DataLengthFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tmd:DataLength>%d</tmd:DataLength>",
            p_req->DataLength);
    }

    if (p_req->DelimiterFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tmd:Delimiter>%s</tmd:Delimiter>",
            p_req->Delimiter);
    }

    return offset;
}

int build_tmd_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:GetServiceCapabilities />");
    return offset;
}

int build_tmd_GetRelayOutputs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:GetRelayOutputs />");
    return offset;
}

int build_tmd_GetRelayOutputOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_GetRelayOutputOptions_REQ * p_req = (tmd_GetRelayOutputOptions_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:GetRelayOutputOptions>");

    if (p_req->RelayOutputTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tmd:RelayOutputToken>%s</tmd:RelayOutputToken>", 
            p_req->RelayOutputToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tmd:GetRelayOutputOptions>");
    
    return offset;
}


int build_tmd_SetRelayOutputSettings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_SetRelayOutputSettings_REQ * p_req = (tmd_SetRelayOutputSettings_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:SetRelayOutputSettings>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:RelayOutput token=\"%s\">", 
        p_req->RelayOutput.token);
    offset += build_RelayOut_xml(p_buf+offset, mlen-offset, &p_req->RelayOutput);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tmd:RelayOutput>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tmd:SetRelayOutputSettings>");
    
    return offset;
}


int build_tmd_SetRelayOutputState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_SetRelayOutputState_REQ * p_req = (tmd_SetRelayOutputState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:SetRelayOutputState>"
            "<tmd:RelayOutputToken>%s</tmd:RelayOutputToken>"
            "<tmd:LogicalState>%s</tmd:LogicalState>"
        "</tmd:SetRelayOutputState>",
        p_req->RelayOutputToken,
        onvif_RelayLogicalStateToString(p_req->LogicalState));

    return offset;        
}

int build_tmd_GetDigitalInputs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:GetDigitalInputs />");
    return offset;
}

int build_tmd_GetDigitalInputConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_GetDigitalInputConfigurationOptions_REQ * p_req = (tmd_GetDigitalInputConfigurationOptions_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:GetDigitalInputConfigurationOptions>");

    if (p_req->TokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tmd:Token>%s</tmd:Token>", 
            p_req->Token);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tmd:GetDigitalInputConfigurationOptions>");
    
    return offset;
}


int build_tmd_SetDigitalInputConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    DigitalInputList * p_dinput;
    tmd_SetDigitalInputConfigurations_REQ * p_req = (tmd_SetDigitalInputConfigurations_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:SetDigitalInputConfigurations>");

    p_dinput = p_req->DigitalInputs;
    while (p_dinput)
    {
        if (p_dinput->DigitalInput.IdleStateFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tmd:DigitalInputs token=\"%s\" IdleState=\"%s\" />",
                p_dinput->DigitalInput.token, 
                onvif_DigitalIdleStateToString(p_dinput->DigitalInput.IdleState));
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tmd:DigitalInputs token=\"%s\" />",
                p_dinput->DigitalInput.token);
        }
        
        p_dinput = p_dinput->next;
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tmd:SetDigitalInputConfigurations>");
    
    return offset;
}

int build_tmd_GetSerialPorts_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:GetSerialPorts />");
    return offset;
}

int build_tmd_GetSerialPortConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_GetSerialPortConfiguration_REQ * p_req = (tmd_GetSerialPortConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:GetSerialPortConfiguration>"
            "<tmd:SerialPortToken>%s</tmd:SerialPortToken>"
        "</tmd:GetSerialPortConfiguration>",
        p_req->SerialPortToken);

    return offset;
}

int build_tmd_SetSerialPortConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_SetSerialPortConfiguration_REQ * p_req = (tmd_SetSerialPortConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:SetSerialPortConfiguration>");

    offset += build_SerialPortConfiguration_xml(p_buf+offset, mlen-offset, &p_req->SerialPortConfiguration);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:ForcePersistance>%s</tmd:ForcePersistance>",
        p_req->ForcePersistance ? "true" : "false");

    offset += snprintf(p_buf+offset, mlen-offset, "</tmd:SetSerialPortConfiguration>");
    
    return offset;
}

int build_tmd_GetSerialPortConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_GetSerialPortConfigurationOptions_REQ * p_req = (tmd_GetSerialPortConfigurationOptions_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:GetSerialPortConfigurationOptions>"
            "<tmd:SerialPortToken>%s</tmd:SerialPortToken>"
        "</tmd:GetSerialPortConfigurationOptions>",
        p_req->SerialPortToken);

    return offset;
}

int build_tmd_SendReceiveSerialCommand_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tmd_SendReceiveSerialCommand_REQ * p_req = (tmd_SendReceiveSerialCommand_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tmd:SendReceiveSerialCommand>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tmd:Token>%s</tmd:Token>",
        p_req->token);
    
    offset += build_SendReceiveSerialCommand_xml(p_buf+offset, mlen-offset, &p_req->Command);
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tmd:SendReceiveSerialCommand>");
    
    return offset;
}

#endif // end of DEVICEIO_SUPPORT


#ifdef PROFILE_G_SUPPORT

int build_RecordingConfiguration_xml(char * p_buf, int mlen, onvif_RecordingConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Source>"
            "<tt:SourceId>%s</tt:SourceId>"
            "<tt:Name>%s</tt:Name>"
            "<tt:Location>%s</tt:Location>"
            "<tt:Description>%s</tt:Description>"
            "<tt:Address>%s</tt:Address>"
        "</tt:Source>"
        "<tt:Content>%s</tt:Content>",
        p_req->Source.SourceId, 
        p_req->Source.Name,
        p_req->Source.Location,
        p_req->Source.Description,
        p_req->Source.Address,
        p_req->Content);

    if (p_req->MaximumRetentionTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:MaximumRetentionTime>PT%dS</tt:MaximumRetentionTime>",
            p_req->MaximumRetentionTime);
    }

    return offset;
}

int build_TrackConfiguration_xml(char * p_buf, int mlen, onvif_TrackConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:TrackType>%s</tt:TrackType>"
        "<tt:Description>%s</tt:Description>",
        onvif_TrackTypeToString(p_req->TrackType),
        p_req->Description);

    return offset;        
}

int build_RecordingJobTrack_xml(char * p_buf, int mlen, onvif_RecordingJobTrack * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Tracks>"
            "<tt:SourceTag>%s</tt:SourceTag>"
            "<tt:Destination>%s</tt:Destination>"
        "</tt:Tracks>",
        p_req->SourceTag,
        p_req->Destination);

    return offset;        
}

int build_RecordingJobSource_xml(char * p_buf, int mlen, onvif_RecordingJobSource * p_req)
{
    uint32 i;
    int offset = 0;

    if (p_req->SourceTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:SourceToken");

        if (p_req->SourceToken.TypeFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Type=\"%s\"", 
                p_req->SourceToken.Type);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, ">");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Token>%s</tt:Token>", 
            p_req->SourceToken.Token);

        offset += snprintf(p_buf+offset, mlen-offset, "</tt:SourceToken>");      
    }
    
    if (p_req->AutoCreateReceiverFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:AutoCreateReceiver>%s</tt:AutoCreateReceiver>",
            p_req->AutoCreateReceiver ? "true" : "false");
    }

    for (i = 0; i < p_req->sizeTracks; i++)
    {
        offset += build_RecordingJobTrack_xml(p_buf+offset, mlen-offset, &p_req->Tracks[i]);
    }
    
    return offset;    
}

int build_RecordingJobConfiguration_xml(char * p_buf, int mlen, onvif_RecordingJobConfiguration * p_req)
{
    uint32 i;
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:RecordingToken>%s</tt:RecordingToken>"
        "<tt:Mode>%s</tt:Mode>"
        "<tt:Priority>%d</tt:Priority>",
        p_req->RecordingToken,
        p_req->Mode,
        p_req->Priority);

    for (i = 0; i < p_req->sizeSource; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:Source>");
        offset += build_RecordingJobSource_xml(p_buf+offset, mlen-offset, &p_req->Source[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tt:Source>");
    }
    
    return offset;
}

int build_SearchScope_xml(char * p_buf, int mlen, onvif_SearchScope * p_req)
{
    uint32 i;
    int offset = 0;
    
    for (i = 0; i < p_req->sizeIncludedSources; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tt:IncludedSources");

        if (p_req->IncludedSources[i].TypeFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Type=\"%s\"", 
                p_req->IncludedSources[i].Type);
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, ">");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:Token>%s</tt:Token>",
            p_req->IncludedSources[i].Token);

        offset += snprintf(p_buf+offset, mlen-offset, "</tt:IncludedSources>");
    }
    
    for (i = 0; i < p_req->sizeIncludedRecordings; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:IncludedRecordings>%s</tt:IncludedRecordings>", 
            p_req->IncludedRecordings[i]);    
    }
    
    if (p_req->RecordingInformationFilterFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RecordingInformationFilter>%s</tt:RecordingInformationFilter>", 
            p_req->RecordingInformationFilter);
    }

    return offset;
}

int build_MetadataFilter_xml(char * p_buf, int mlen, onvif_MetadataFilter * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:MetadataStreamFilter>%s</tt:MetadataStreamFilter>",
        p_req->MetadataStreamFilter);
    
    return offset;
}

int build_PTZPositionFilter_xml(char * p_buf, int mlen, onvif_PTZPositionFilter * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:MinPosition>");
    offset += build_PTZVector_xml(p_buf+offset, mlen-offset, &p_req->MinPosition);
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:MinPosition>");

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:MaxPosition>");
    offset += build_PTZVector_xml(p_buf+offset, mlen-offset, &p_req->MaxPosition);
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:MaxPosition>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:EnterOrExit>%s</tt:EnterOrExit>",
        p_req->EnterOrExit ? "true" : "false");
    
    return offset;
}

int build_StorageReferencePath_xml(char * p_buf, int mlen, onvif_StorageReferencePath * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:StorageToken>%s</tt:StorageToken>",
        p_req->StorageToken);

    if (p_req->RelativePath[0] != '\0')
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tt:RelativePath>%s</tt:RelativePath>",
            p_req->RelativePath);
    }
    
    return offset;
}

int build_trc_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:GetServiceCapabilities />");
    return offset;
}

int build_trc_CreateRecording_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_CreateRecording_REQ * p_req = (trc_CreateRecording_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:CreateRecording>"
        "<trc:RecordingConfiguration>");
    offset += build_RecordingConfiguration_xml(p_buf+offset, mlen-offset, &p_req->RecordingConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trc:RecordingConfiguration>"
        "</trc:CreateRecording>");
    
    return offset;
}
  
int build_trc_DeleteRecording_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_DeleteRecording_REQ * p_req = (trc_DeleteRecording_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:DeleteRecording>"
            "<trc:RecordingToken>%s</trc:RecordingToken>"
        "</trc:DeleteRecording>",
        p_req->RecordingToken);
    
    return offset;
}
 
int build_trc_GetRecordings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:GetRecordings />");
    return offset;
}
 
int build_trc_SetRecordingConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_SetRecordingConfiguration_REQ * p_req = (trc_SetRecordingConfiguration_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:SetRecordingConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:RecordingToken>%s</trc:RecordingToken>",
        p_req->RecordingToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:RecordingConfiguration>");
    offset += build_RecordingConfiguration_xml(p_buf+offset, mlen-offset, &p_req->RecordingConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:RecordingConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, "</trc:SetRecordingConfiguration>");
    
    return offset;
}
 
int build_trc_GetRecordingConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_GetRecordingConfiguration_REQ * p_req = (trc_GetRecordingConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetRecordingConfiguration>"
            "<trc:RecordingToken>%s</trc:RecordingToken>"
        "</trc:GetRecordingConfiguration>",
        p_req->RecordingToken);
    
    return offset;
}
 
int build_trc_GetRecordingOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_GetRecordingConfiguration_REQ * p_req = (trc_GetRecordingConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetRecordingOptions>"
            "<trc:RecordingToken>%s</trc:RecordingToken>"
        "</trc:GetRecordingOptions>",
        p_req->RecordingToken);
    
    return offset;
}

int build_trc_CreateTrack_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_CreateTrack_REQ * p_req = (trc_CreateTrack_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:CreateTrack>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:RecordingToken>%s</trc:RecordingToken>",
        p_req->RecordingToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:TrackConfiguration>");        
    offset += build_TrackConfiguration_xml(p_buf+offset, mlen-offset, &p_req->TrackConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:TrackConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, "</trc:CreateTrack>");
    
    return offset;
}
 
int build_trc_DeleteTrack_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_DeleteTrack_REQ * p_req = (trc_DeleteTrack_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:DeleteTrack>"
            "<trc:RecordingToken>%s</trc:RecordingToken>"
            "<trc:TrackToken>%s</trc:TrackToken>"
        "</trc:DeleteTrack>",
        p_req->RecordingToken,
        p_req->TrackToken);
    
    return offset;
}
 
int build_trc_GetTrackConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_GetTrackConfiguration_REQ * p_req = (trc_GetTrackConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetTrackConfiguration>"
            "<trc:RecordingToken>%s</trc:RecordingToken>"
            "<trc:TrackToken>%s</trc:TrackToken>"
        "</trc:GetTrackConfiguration>",
        p_req->RecordingToken,
        p_req->TrackToken);
    
    return offset;
}
 
int build_trc_SetTrackConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_SetTrackConfiguration_REQ * p_req = (trc_SetTrackConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:SetTrackConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:RecordingToken>%s</trc:RecordingToken>"
        "<trc:TrackToken>%s</trc:TrackToken>",
        p_req->RecordingToken,
        p_req->TrackToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:TrackConfiguration>");        
    offset += build_TrackConfiguration_xml(p_buf+offset, mlen-offset, &p_req->TrackConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:TrackConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, "</trc:SetTrackConfiguration>");
    
    return offset;
}

int build_trc_CreateRecordingJob_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_CreateRecordingJob_REQ * p_req = (trc_CreateRecordingJob_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:CreateRecordingJob>"
        "<trc:JobConfiguration>");
    offset += build_RecordingJobConfiguration_xml(p_buf+offset, mlen-offset, &p_req->JobConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</trc:JobConfiguration>"
        "</trc:CreateRecordingJob>");
    
    return offset;
}
 
int build_trc_DeleteRecordingJob_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_DeleteRecordingJob_REQ * p_req = (trc_DeleteRecordingJob_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:DeleteRecordingJob>"
            "<trc:JobToken>%s</trc:JobToken>"
        "</trc:DeleteRecordingJob>",
        p_req->JobToken);
    
    return offset;
}
 
int build_trc_GetRecordingJobs_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:GetRecordingJobs />");
    return offset;
}
 
int build_trc_SetRecordingJobConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_SetRecordingJobConfiguration_REQ * p_req = (trc_SetRecordingJobConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:SetRecordingJobConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:JobToken>%s</trc:JobToken>",
        p_req->JobToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:JobConfiguration>");        
    offset += build_RecordingJobConfiguration_xml(p_buf+offset, mlen-offset, &p_req->JobConfiguration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:JobConfiguration>");

    offset += snprintf(p_buf+offset, mlen-offset, "</trc:SetRecordingJobConfiguration>");
    
    return offset;
}
 
int build_trc_GetRecordingJobConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_SetRecordingJobConfiguration_REQ * p_req = (trc_SetRecordingJobConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetRecordingJobConfiguration>"
            "<trc:JobToken>%s</trc:JobToken>"
        "</trc:GetRecordingJobConfiguration>",
        p_req->JobToken);
    
    return offset;
}
 
int build_trc_SetRecordingJobMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_SetRecordingJobMode_REQ * p_req = (trc_SetRecordingJobMode_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:SetRecordingJobMode>"
            "<trc:JobToken>%s</trc:JobToken>"
            "<trc:Mode>%s</trc:Mode>"
        "</trc:SetRecordingJobMode>",
        p_req->JobToken,
        p_req->Mode);
    
    return offset;
}
 
int build_trc_GetRecordingJobState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_GetRecordingJobState_REQ * p_req = (trc_GetRecordingJobState_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetRecordingJobState>"
            "<trc:JobToken>%s</trc:JobToken>"
        "</trc:GetRecordingJobState>",
        p_req->JobToken);
    
    return offset;
}

int build_trc_ExportRecordedData_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_ExportRecordedData_REQ * p_req = (trc_ExportRecordedData_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trc:ExportRecordedData>");

    if (p_req->StartPoint != 0)
    {
        char strtime[64];
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trc:StartPoint>%s</trc:StartPoint>",
            onvif_format_datetime_str(p_req->StartPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));
    }

    if (p_req->EndPoint != 0)
    {
        char strtime[64];
        
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<trc:EndPoint>%s</trc:EndPoint>",
            onvif_format_datetime_str(p_req->EndPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));
    }

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:SearchScope>");
    offset += build_SearchScope_xml(p_buf+offset, mlen-offset, &p_req->SearchScope);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:SearchScope>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:FileFormat>%s</trc:FileFormat>",
        p_req->FileFormat);

    offset += snprintf(p_buf+offset, mlen-offset, "<trc:StorageDestination>");
    offset += build_StorageReferencePath_xml(p_buf+offset, mlen-offset, &p_req->StorageDestination);
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:StorageDestination>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</trc:ExportRecordedData>");
    
    return offset;
}

int build_trc_StopExportRecordedData_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_StopExportRecordedData_REQ * p_req = (trc_StopExportRecordedData_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:StopExportRecordedData>"
            "<trc:OperationToken>%s</trc:OperationToken>"
        "</trc:StopExportRecordedData>",
        p_req->OperationToken);
    
    return offset;
}

int build_trc_GetExportRecordedDataState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trc_GetExportRecordedDataState_REQ * p_req = (trc_GetExportRecordedDataState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trc:GetExportRecordedDataState>"
            "<trc:OperationToken>%s</trc:OperationToken>"
        "</trc:GetExportRecordedDataState>",
        p_req->OperationToken);
    
    return offset;
}

int build_trp_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trp:GetServiceCapabilities />");
    return offset;
}

int build_trp_GetReplayUri_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trp_GetReplayUri_REQ * p_req = (trp_GetReplayUri_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trp:GetReplayUri>"
            "<trp:StreamSetup>"
                "<tt:Stream>%s</tt:Stream>"
                "<tt:Transport>"
                    "<tt:Protocol>%s</tt:Protocol>"
                "</tt:Transport>"
            "</trp:StreamSetup>"
            "<trp:RecordingToken>%s</trp:RecordingToken>"
        "</trp:GetReplayUri>",
        onvif_StreamTypeToString(p_req->StreamSetup.Stream),
        onvif_TransportProtocolToString(p_req->StreamSetup.Transport.Protocol),
        p_req->RecordingToken);

    return offset;
}

int build_trp_GetReplayConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trp:GetReplayConfiguration />");
    return offset;
}

int build_trp_SetReplayConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trp_SetReplayConfiguration_REQ * p_req = (trp_SetReplayConfiguration_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trp:SetReplayConfiguration>"
            "<trp:Configuration>"
                "<tt:SessionTimeout>PT%dS</tt:SessionTimeout>"
            "</trp:Configuration>"
        "</trp:SetReplayConfiguration>",
        p_req->Configuration.SessionTimeout);
    
    return offset;
}

int build_tse_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetServiceCapabilities />");
    return offset;
}

int build_tse_GetRecordingSummary_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetRecordingSummary />");
    return offset;
}

int build_tse_GetRecordingInformation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tse_GetRecordingInformation_REQ * p_req = (tse_GetRecordingInformation_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:GetRecordingInformation>"
            "<tse:RecordingToken>%s</tse:RecordingToken>"
        "</tse:GetRecordingInformation>",
        p_req->RecordingToken);
    
    return offset;
}

int build_tse_GetMediaAttributes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    char strtime[64] = {'\0'};
    tse_GetMediaAttributes_REQ * p_req = (tse_GetMediaAttributes_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetMediaAttributes>");

    for (i = 0; i < 10; i++)
    {
        if (p_req->RecordingTokens[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tse:RecordingTokens>%s</tse:RecordingTokens>", 
                p_req->RecordingTokens[i]);
        }
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:Time>%s</tse:Time>", 
        onvif_format_datetime_str(p_req->Time, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));

    offset += snprintf(p_buf+offset, mlen-offset, "</tse:GetMediaAttributes>");
    
    return offset;
}

int build_tse_FindRecordings_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    tse_FindRecordings_REQ * p_req = (tse_FindRecordings_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:FindRecordings>");    

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:Scope>");
    offset += build_SearchScope_xml(p_buf+offset, mlen-offset, &p_req->Scope);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:Scope>");
    
    if (p_req->MaxMatchesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxMatches>%d</tse:MaxMatches>", 
            p_req->MaxMatches);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:KeepAliveTime>PT%dS</tse:KeepAliveTime>", 
        p_req->KeepAliveTime);
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:FindRecordings>");
    
    return offset;
}

int build_tse_GetRecordingSearchResults_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    tse_GetRecordingSearchResults_REQ * p_req = (tse_GetRecordingSearchResults_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetRecordingSearchResults>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:SearchToken>%s</tse:SearchToken>", 
        p_req->SearchToken);

    if (p_req->MinResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MinResults>%d</tse:MinResults>", 
            p_req->MinResults);
    }
    
    if (p_req->MaxResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxResults>%d</tse:MaxResults>", 
            p_req->MaxResults);
    }
    
    if (p_req->WaitTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:WaitTime>PT%dS</tse:WaitTime>", 
            p_req->WaitTime);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:GetRecordingSearchResults>");
    
    return offset;
}

int build_tse_FindEvents_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    char strtime[64] = {'\0'};
    tse_FindEvents_REQ * p_req = (tse_FindEvents_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:FindEvents>");    

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:StartPoint>%s</tse:StartPoint>", 
        onvif_format_datetime_str(p_req->StartPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));

    if (p_req->EndPointFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:EndPoint>%s</tse:EndPoint>", 
            onvif_format_datetime_str(p_req->EndPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));
    }

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:Scope>");
    offset += build_SearchScope_xml(p_buf+offset, mlen-offset, &p_req->Scope);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:Scope>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tse:SearchFilter>");
    offset += build_EventFilter_xml(p_buf+offset, mlen-offset, &p_req->SearchFilter);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:SearchFilter>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:IncludeStartState>%s</tse:IncludeStartState>", 
        p_req->IncludeStartState ? "true" : "false");

    if (p_req->MaxMatchesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxMatches>%d</tse:MaxMatches>", 
            p_req->MaxMatches);
    }    
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:KeepAliveTime>PT%dS</tse:KeepAliveTime>", 
        p_req->KeepAliveTime);
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:FindEvents>");
    
    return offset;
}

int build_tse_GetEventSearchResults_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    tse_GetEventSearchResults_REQ * p_req = (tse_GetEventSearchResults_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetEventSearchResults>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:SearchToken>%s</tse:SearchToken>", 
        p_req->SearchToken);

    if (p_req->MinResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MinResults>%d</tse:MinResults>", 
            p_req->MinResults);
    }
    
    if (p_req->MaxResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxResults>%d</tse:MaxResults>", 
            p_req->MaxResults);
    }
    
    if (p_req->WaitTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:WaitTime>PT%dS</tse:WaitTime>", 
            p_req->WaitTime);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:GetEventSearchResults>");
    
    return offset;
}

int build_tse_FindMetadata_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    char strtime[64] = {'\0'};
    tse_FindMetadata_REQ * p_req = (tse_FindMetadata_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:FindMetadata>");    

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:StartPoint>%s</tse:StartPoint>", 
        onvif_format_datetime_str(p_req->StartPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));

    if (p_req->EndPointFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:EndPoint>%s</tse:EndPoint>", 
            onvif_format_datetime_str(p_req->EndPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));
    }

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:Scope>");
    offset += build_SearchScope_xml(p_buf+offset, mlen-offset, &p_req->Scope);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:Scope>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tse:MetadataFilter>");
    offset += build_MetadataFilter_xml(p_buf+offset, mlen-offset, &p_req->MetadataFilter);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:MetadataFilter>");
    
    if (p_req->MaxMatchesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxMatches>%d</tse:MaxMatches>", 
            p_req->MaxMatches);
    }    
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:KeepAliveTime>PT%dS</tse:KeepAliveTime>", 
        p_req->KeepAliveTime);
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:FindMetadata>");
    
    return offset;
}

int build_tse_GetMetadataSearchResults_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tse_GetMetadataSearchResults_REQ * p_req = (tse_GetMetadataSearchResults_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetMetadataSearchResults>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:SearchToken>%s</tse:SearchToken>", 
        p_req->SearchToken);

    if (p_req->MinResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MinResults>%d</tse:MinResults>", 
            p_req->MinResults);
    }
    
    if (p_req->MaxResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxResults>%d</tse:MaxResults>", 
            p_req->MaxResults);
    }
    
    if (p_req->WaitTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:WaitTime>PT%dS</tse:WaitTime>", 
            p_req->WaitTime);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:GetMetadataSearchResults>");
    
    return offset;
}

int build_tse_FindPTZPosition_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    char strtime[64] = {'\0'};
    tse_FindPTZPosition_REQ * p_req = (tse_FindPTZPosition_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:FindPTZPosition>");    

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:StartPoint>%s</tse:StartPoint>", 
        onvif_format_datetime_str(p_req->StartPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));

    if (p_req->EndPointFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:EndPoint>%s</tse:EndPoint>", 
            onvif_format_datetime_str(p_req->EndPoint, 1, "%Y-%m-%dT%H:%M:%SZ", strtime, sizeof(strtime)));
    }

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:Scope>");
    offset += build_SearchScope_xml(p_buf+offset, mlen-offset, &p_req->Scope);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:Scope>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tse:SearchFilter>");
    offset += build_PTZPositionFilter_xml(p_buf+offset, mlen-offset, &p_req->SearchFilter);
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:SearchFilter>");
    
    if (p_req->MaxMatchesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxMatches>%d</tse:MaxMatches>", 
            p_req->MaxMatches);
    }    
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:KeepAliveTime>PT%dS</tse:KeepAliveTime>", 
        p_req->KeepAliveTime);
        
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:FindPTZPosition>");
    
    return offset;
}

int build_tse_GetPTZPositionSearchResults_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tse_GetPTZPositionSearchResults_REQ * p_req = (tse_GetPTZPositionSearchResults_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tse:GetPTZPositionSearchResults>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:SearchToken>%s</tse:SearchToken>", 
        p_req->SearchToken);

    if (p_req->MinResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MinResults>%d</tse:MinResults>", 
            p_req->MinResults);
    }
    
    if (p_req->MaxResultsFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:MaxResults>%d</tse:MaxResults>", 
            p_req->MaxResults);
    }
    
    if (p_req->WaitTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tse:WaitTime>PT%dS</tse:WaitTime>", 
            p_req->WaitTime);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tse:GetPTZPositionSearchResults>");
    
    return offset;
}

int build_tse_GetSearchState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tse_GetSearchState_REQ * p_req = (tse_GetSearchState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:GetSearchState>"
            "<tse:SearchToken>%s</tse:SearchToken>"
        "</tse:GetSearchState>",
        p_req->SearchToken);
    
    return offset;
}

int build_tse_EndSearch_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tse_EndSearch_REQ * p_req = (tse_EndSearch_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tse:EndSearch>"
            "<tse:SearchToken>%s</tse:SearchToken>"
        "</tse:EndSearch>",
        p_req->SearchToken);
    
    return offset;
}

#endif // end of PROFILE_G_SUPPORT

#ifdef PROFILE_C_SUPPORT

int build_AccessPointInfo_xml(char * p_buf, int mlen, onvif_AccessPointInfo * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Name>%s</tac:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:Description>%s</tac:Description>", 
            p_req->Description);
    }
    
    if (p_req->AreaFromFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:AreaFrom>%s</tac:AreaFrom>", 
            p_req->AreaFrom);
    }
    
    if (p_req->AreaToFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:AreaTo>%s</tac:AreaTo>", 
            p_req->AreaTo);
    }
    
    if (p_req->EntityTypeFlag)
    {
        if (p_req->EntityTypeAttrFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:EntityType %s>%s</tac:EntityType>", 
                p_req->EntityTypeAttr, 
                p_req->EntityType);
        }
        else
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:EntityType>%s</tac:EntityType>", 
                p_req->EntityType);
        }
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Entity>%s</tac:Entity>", 
        p_req->Entity);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Capabilities "
            "DisableAccessPoint=\"%s\" "
            "Duress=\"%s\" "
            "AnonymousAccess=\"%s\" "
            "AccessTaken=\"%s\" "
            "ExternalAuthorization=\"%s\" "
            "IdentiferAccess=\"%s\" ",
        p_req->Capabilities.DisableAccessPoint ? "true" : "false",
        p_req->Capabilities.Duress ? "true" : "false",
        p_req->Capabilities.AnonymousAccess ? "true" : "false",
        p_req->Capabilities.AccessTaken ? "true" : "false",
        p_req->Capabilities.ExternalAuthorization ? "true" : "false",
        p_req->Capabilities.IdentifierAccess ? "true" : "false");

    if (p_req->Capabilities.SupportedRecognitionTypesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "SupportedRecognitionTypes=\"%s\" ",
            p_req->Capabilities.SupportedRecognitionTypes);
    }

    if (p_req->Capabilities.SupportedFeedbackTypesFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "SupportedFeedbackTypes=\"%s\" ",
            p_req->Capabilities.SupportedFeedbackTypes);
    }

    offset += snprintf(p_buf+offset, mlen-offset, " />");
    
    return offset;
}

int build_AreaInfo_xml(char * p_buf, int mlen, onvif_AreaInfo * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Name>%s</tac:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:Description>%s</tac:Description>", 
            p_req->Description);
    }

    return offset;
}

int build_DoorInfo_xml(char * p_buf, int mlen, onvif_DoorInfo * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:Name>%s</tdc:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:Description>%s</tdc:Description>", 
            p_req->Description);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:Capabilities "
            "Access=\"%s\" "
            "AccessTimingOverride=\"%s\" "
            "Lock=\"%s\" "
            "Unlock=\"%s\" "
            "Block=\"%s\" "
            "DoubleLock=\"%s\" "
            "LockDown=\"%s\" "
            "LockOpen=\"%s\" "
            "DoorMonitor=\"%s\" "
            "LockMonitor=\"%s\" "
            "DoubleLockMonitor=\"%s\" "
            "Alarm=\"%s\" "
            "Tamper=\"%s\" "
            "Fault=\"%s\" />",
        p_req->Capabilities.Access ? "true" : "false",
        p_req->Capabilities.AccessTimingOverride ? "true" : "false",
        p_req->Capabilities.Lock ? "true" : "false",
        p_req->Capabilities.Unlock ? "true" : "false",
        p_req->Capabilities.Block ? "true" : "false",
        p_req->Capabilities.DoubleLock ? "true" : "false",
        p_req->Capabilities.LockDown ? "true" : "false",
        p_req->Capabilities.LockOpen ? "true" : "false",
        p_req->Capabilities.DoorMonitor ? "true" : "false",
        p_req->Capabilities.LockMonitor ? "true" : "false",
        p_req->Capabilities.DoubleLockMonitor ? "true" : "false",
        p_req->Capabilities.Alarm ? "true" : "false",
        p_req->Capabilities.Tamper ? "true" : "false",
        p_req->Capabilities.Fault ? "true" : "false");
    
    return offset;
}

int build_Timings_xml(char * p_buf, int mlen, onvif_Timings * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:ReleaseTime>PT%uS</tdc:ReleaseTime>"
        "<tdc:OpenTime>PT%uS</tdc:OpenTime>",
        p_req->ReleaseTime,
        p_req->OpenTime);

    if (p_req->ExtendedReleaseTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:ExtendedReleaseTime>PT%uS</tdc:ExtendedReleaseTime>",
            p_req->ExtendedReleaseTime);
    }

    if (p_req->DelayTimeBeforeRelockFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:DelayTimeBeforeRelock>PT%uS</tdc:DelayTimeBeforeRelock>",
            p_req->DelayTimeBeforeRelock);
    }

    if (p_req->ExtendedOpenTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:ExtendedOpenTime>PT%uS</tdc:ExtendedOpenTime>",
            p_req->ExtendedOpenTime);
    }

    if (p_req->PreAlarmTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:PreAlarmTime>PT%uS</tdc:PreAlarmTime>",
            p_req->PreAlarmTime);
    }
    
    return offset;     
}

int build_Door_xml(char * p_buf, int mlen, onvif_Door * p_req)
{
    int offset = 0;
    
    offset += build_DoorInfo_xml(p_buf+offset, mlen-offset, &p_req->DoorInfo);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:DoorType>%s</tdc:DoorType>", 
        p_req->DoorType);

    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:Timings>");
    offset += build_Timings_xml(p_buf+offset, mlen-offset, &p_req->Timings);
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:Timings>");
    
    return offset;
}

int build_tac_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetServiceCapabilities />");
    return offset;
}

int build_tac_GetAccessPointInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_GetAccessPointInfoList_REQ * p_req = (tac_GetAccessPointInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAccessPointInfoList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Limit>%d</tac:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:StartReference>%s</tac:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAccessPointInfoList>");

    return offset;
}
   
int build_tac_GetAccessPointInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tac_GetAccessPointInfo_REQ * p_req = (tac_GetAccessPointInfo_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAccessPointInfo>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Token>%s</tac:Token>",
                p_req->token[i]);
        }
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAccessPointInfo>");

    return offset;
}

int build_tac_GetAccessPointList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_GetAccessPointList_REQ * p_req = (tac_GetAccessPointList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAccessPointList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Limit>%d</tac:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:StartReference>%s</tac:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAccessPointList>");

    return offset;
}

int build_tac_GetAccessPoints_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tac_GetAccessPoints_REQ * p_req = (tac_GetAccessPoints_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAccessPoints>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Token>%s</tdc:Token>",
                p_req->token[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAccessPoints>");

    return offset;
}

int build_tac_CreateAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_CreateAccessPoint_REQ * p_req = (tac_CreateAccessPoint_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:CreateAccessPoint>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:AccessPoint token=\"%s\">", 
        p_req->AccessPoint.token);

    offset += build_AccessPointInfo_xml(p_buf+offset, mlen-offset, &p_req->AccessPoint);

    if (p_req->AuthenticationProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:AuthenticationProfileToken>%s</tac:AuthenticationProfileToken>",
            p_req->AuthenticationProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:AccessPoint>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:CreateAccessPoint>");

    return offset;
}

int build_tac_SetAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_CreateAccessPoint_REQ * p_req = (tac_CreateAccessPoint_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:SetAccessPoint>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:AccessPoint token=\"%s\">", 
        p_req->AccessPoint.token);

    offset += build_AccessPointInfo_xml(p_buf+offset, mlen-offset, &p_req->AccessPoint);

    if (p_req->AuthenticationProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:AuthenticationProfileToken>%s</tac:AuthenticationProfileToken>",
            p_req->AuthenticationProfileToken);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:AccessPoint>");
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:SetAccessPoint>");

    return offset;
}

int build_tac_ModifyAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)

{
    int offset = 0;
    tac_CreateAccessPoint_REQ * p_req = (tac_CreateAccessPoint_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:ModifyAccessPoint>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:AccessPoint token=\"%s\">", 
        p_req->AccessPoint.token);

    offset += build_AccessPointInfo_xml(p_buf+offset, mlen-offset, &p_req->AccessPoint);

    if (p_req->AuthenticationProfileTokenFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tac:AuthenticationProfileToken>%s</tac:AuthenticationProfileToken>",
            p_req->AuthenticationProfileToken);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:AccessPoint>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:ModifyAccessPoint>");

    return offset;
}

int build_tac_DeleteAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_DeleteAccessPoint_REQ * p_req = (tac_DeleteAccessPoint_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:DeleteAccessPoint>"
            "<tac:Token>%s</tac:Token>"
        "</tac:DeleteAccessPoint>",
        p_req->Token);

    return offset;
}

int build_tac_GetAreaInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_GetAreaInfoList_REQ * p_req = (tac_GetAreaInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAreaInfoList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Limit>%d</tac:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:StartReference>%s</tac:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAreaInfoList>");

    return offset;
}
 
int build_tac_GetAreaInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tac_GetAreaInfo_REQ * p_req = (tac_GetAreaInfo_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAreaInfo>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Token>%s</tac:Token>",
                p_req->token[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAreaInfo>");

    return offset;
}

int build_tac_GetAreaList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_GetAreaList_REQ * p_req = (tac_GetAreaList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAreaList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Limit>%d</tac:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:StartReference>%s</tac:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAccessPointList>");

    return offset;
}

int build_tac_GetAreas_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tac_GetAreas_REQ * p_req = (tac_GetAreas_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tac:GetAreas>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tac:Token>%s</tdc:Token>",
                p_req->token[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:GetAreas>");

    return offset;
}

int build_tac_CreateArea_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_CreateArea_REQ * p_req = (tac_CreateArea_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:CreateArea>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Area token=\"%s\">", 
        p_req->Area.token);
    offset += build_AreaInfo_xml(p_buf+offset, mlen-offset, &p_req->Area);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:Area>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:CreateArea>");

    return offset;
}

int build_tac_SetArea_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_SetArea_REQ * p_req = (tac_SetArea_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:SetArea>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Area token=\"%s\">", 
        p_req->Area.token);
    offset += build_AreaInfo_xml(p_buf+offset, mlen-offset, &p_req->Area);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:Area>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:SetArea>");

    return offset;
}

int build_tac_ModifyArea_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_ModifyArea_REQ * p_req = (tac_ModifyArea_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tac:ModifyArea>");
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:Area token=\"%s\">", 
        p_req->Area.token);
    offset += build_AreaInfo_xml(p_buf+offset, mlen-offset, &p_req->Area);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tac:Area>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tac:ModifyArea>");

    return offset;
}

int build_tac_DeleteArea_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_DeleteArea_REQ * p_req = (tac_DeleteArea_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:DeleteArea>"
            "<tac:Token>%s</tac:Token>"
        "</tac:DeleteArea>",
        p_req->Token);

    return offset;
}

int build_tac_GetAccessPointState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_GetAccessPointState_REQ * p_req = (tac_GetAccessPointState_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:GetAccessPointState>"
            "<tac:Token>%s</tac:Token>"
        "</tac:GetAccessPointState>",
        p_req->Token);

    return offset;
}

int build_tac_EnableAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_EnableAccessPoint_REQ * p_req = (tac_EnableAccessPoint_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:EnableAccessPoint>"
            "<tac:Token>%s</tac:Token>"
        "</tac:EnableAccessPoint>",
        p_req->Token);

    return offset;
}
 
int build_tac_DisableAccessPoint_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tac_DisableAccessPoint_REQ * p_req = (tac_DisableAccessPoint_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tac:DisableAccessPoint>"
            "<tac:Token>%s</tac:Token>"
        "</tac:DisableAccessPoint>",
        p_req->Token);

    return offset;
}

int build_tdc_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:GetServiceCapabilities />");
    return offset;
}

int build_tdc_GetDoorInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_GetDoorInfoList_REQ * p_req = (tdc_GetDoorInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:GetDoorInfoList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:Limit>%d</tdc:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:StartReference>%s</tdc:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:GetDoorInfoList>");

    return offset;
}
 
int build_tdc_GetDoorInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tdc_GetDoorInfo_REQ * p_req = (tdc_GetDoorInfo_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:GetDoorInfo>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:Token>%s</tdc:Token>",
                p_req->token[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:GetDoorInfo>");

    return offset;
}
 
int build_tdc_GetDoorState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_GetDoorState_REQ * p_req = (tdc_GetDoorState_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:GetDoorState>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:GetDoorState>",
        p_req->Token);

    return offset;
}
 
int build_tdc_AccessDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_AccessDoor_REQ * p_req = (tdc_AccessDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:AccessDoor>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:Token>%s</tdc:Token>", 
        p_req->Token);

    if (p_req->UseExtendedTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:UseExtendedTime>%s</tdc:UseExtendedTime>",
            p_req->UseExtendedTime ? "true" : "false");
    }
    
    if (p_req->AccessTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:AccessTime>PT%dS</tdc:AccessTime>",
            p_req->AccessTime);
    }
    
    if (p_req->OpenTooLongTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:OpenTooLongTime>PT%dS</tdc:OpenTooLongTime>",
            p_req->OpenTooLongTime);
    }
    
    if (p_req->PreAlarmTimeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tdc:PreAlarmTime>PT%dS</tdc:PreAlarmTime>",
            p_req->PreAlarmTime);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:AccessDoor>");

    return offset;
}
 
int build_tdc_LockDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_LockDoor_REQ * p_req = (tdc_LockDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:LockDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:LockDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_UnlockDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_UnlockDoor_REQ * p_req = (tdc_UnlockDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:UnlockDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:UnlockDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_DoubleLockDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_DoubleLockDoor_REQ * p_req = (tdc_DoubleLockDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:DoubleLockDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:DoubleLockDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_BlockDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_BlockDoor_REQ * p_req = (tdc_BlockDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:BlockDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:BlockDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_LockDownDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_LockDownDoor_REQ * p_req = (tdc_LockDownDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:LockDownDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:LockDownDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_LockDownReleaseDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_LockDownReleaseDoor_REQ * p_req = (tdc_LockDownReleaseDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:LockDownReleaseDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:LockDownReleaseDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_LockOpenDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_LockOpenDoor_REQ * p_req = (tdc_LockOpenDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:LockOpenDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:LockOpenDoor>",
        p_req->Token);

    return offset;
}
 
int build_tdc_LockOpenReleaseDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_LockOpenReleaseDoor_REQ * p_req = (tdc_LockOpenReleaseDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:LockOpenReleaseDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:LockOpenReleaseDoor>",
        p_req->Token);

    return offset;
}

int build_tdc_GetDoors_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    uint32 i;
    int offset = 0;
    tdc_GetDoors_REQ * p_req = (tdc_GetDoors_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:GetDoors>");

    for (i = 0; i < ARRAY_SIZE(p_req->token); i++)
    {
        if (p_req->token[i][0] != '\0')
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:Token>%s</tdc:Token>",
                p_req->token[i]);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:GetDoors>");

    return offset;
}

int build_tdc_GetDoorList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_GetDoorList_REQ * p_req = (tdc_GetDoorList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:GetDoorList>");

    if (p_req)
    {
        if (p_req->LimitFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:Limit>%d</tdc:Limit>", 
                p_req->Limit);
        }

        if (p_req->StartReferenceFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tdc:StartReference>%s</tdc:StartReference>", 
                p_req->StartReference);
        }
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:GetDoorList>");

    return offset;
}

int build_tdc_CreateDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_CreateDoor_REQ * p_req = (tdc_CreateDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:CreateDoor>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:Door token=\"%s\">", p_req->Door.DoorInfo.token);
    offset += build_Door_xml(p_buf+offset, mlen-offset, &p_req->Door);
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:Door>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:CreateDoor>");

    return offset;
}

int build_tdc_SetDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_SetDoor_REQ * p_req = (tdc_SetDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:SetDoor>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:Door token=\"%s\">", p_req->Door.DoorInfo.token);
    offset += build_Door_xml(p_buf+offset, mlen-offset, &p_req->Door);
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:Door>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:SetDoor>");

    return offset;
}

int build_tdc_ModifyDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_ModifyDoor_REQ * p_req = (tdc_ModifyDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:ModifyDoor>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tdc:Door token=\"%s\">", p_req->Door.DoorInfo.token);
    offset += build_Door_xml(p_buf+offset, mlen-offset, &p_req->Door);
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:Door>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tdc:ModifyDoor>");

    return offset;
}

int build_tdc_DeleteDoor_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tdc_DeleteDoor_REQ * p_req = (tdc_DeleteDoor_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tdc:DeleteDoor>"
            "<tdc:Token>%s</tdc:Token>"
        "</tdc:DeleteDoor>",
        p_req->Token);

    return offset;
}

#endif // end of PROFILE_C_SUPPORT

#ifdef THERMAL_SUPPORT

int build_ColorPalette_xml(char * p_buf, int mlen, onvif_ColorPalette * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:ColorPalette token=\"%s\" Type=\"%s\">"
        "<tth:Name>%s</tth:Name>"
        "</tth:ColorPalette>",
        p_req->token, 
        p_req->Type,
        p_req->Name);

    return offset;        
}

int build_NUCTable_xml(char * p_buf, int mlen, onvif_NUCTable * p_req)
{
    int offset = 0;
    char buff[32];
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:NUCTable token=\"%s\"", 
        p_req->token);

    if (p_req->LowTemperatureFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " LowTemperature=\"%s\"", 
            onvif_format_float_num(p_req->LowTemperature, 6, buff, sizeof(buff)-1));
    }
    
    if (p_req->HighTemperatureFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            " HighTemperature=\"%s\"", 
            onvif_format_float_num(p_req->HighTemperature, 6, buff, sizeof(buff)-1));
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, ">");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:Name>%s</tth:Name>", 
        p_req->Name);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tth:NUCTable>"); 

    return offset;        
}

int build_ThermalConfiguration_xml(char * p_buf, int mlen, onvif_ThermalConfiguration * p_req)
{
    int offset = 0;
    char buff[32];
    
    offset += build_ColorPalette_xml(p_buf+offset, mlen-offset, &p_req->ColorPalette);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:Polarity>%s</tth:Polarity>",
        onvif_PolarityToString(p_req->Polarity));

    if (p_req->NUCTableFlag)
    {
        offset += build_NUCTable_xml(p_buf+offset, mlen-offset, &p_req->NUCTable);     
    }

    if (p_req->CoolerFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tth:Cooler>");

        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:Enabled>%s</tth:Enabled>", 
            p_req->Cooler.Enabled ? "true" : "false");

        if (p_req->Cooler.RunTimeFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                "<tth:RunTime>%s</tth:RunTime>", 
                onvif_format_float_num(p_req->Cooler.RunTime, 6, buff, sizeof(buff)-1));
        }
        
        offset += snprintf(p_buf+offset, mlen-offset, "</tth:Cooler>");
    }

    return offset;
}

int build_RadiometryGlobalParameters_xml(char * p_buf, int mlen, onvif_RadiometryGlobalParameters * p_req)
{
    int offset = 0;
    char buff1[32], buff2[32], buff3[32];
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:ReflectedAmbientTemperature>%s</tth:ReflectedAmbientTemperature>"
        "<tth:Emissivity>%s</tth:Emissivity>"
        "<tth:DistanceToObject>%s</tth:DistanceToObject>",
        onvif_format_float_num(p_req->ReflectedAmbientTemperature, 6, buff1, sizeof(buff1)-1),
        onvif_format_float_num(p_req->Emissivity, 6, buff2, sizeof(buff2)-1),
        onvif_format_float_num(p_req->DistanceToObject, 6, buff3, sizeof(buff3)-1));

    if (p_req->RelativeHumidityFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:RelativeHumidity>%s</tth:RelativeHumidity>",
            onvif_format_float_num(p_req->RelativeHumidity, 6, buff1, sizeof(buff1)-1));
    }

    if (p_req->AtmosphericTemperatureFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:AtmosphericTemperature>%s</tth:AtmosphericTemperature>",
            onvif_format_float_num(p_req->AtmosphericTemperature, 6, buff1, sizeof(buff1)-1));
    }

    if (p_req->AtmosphericTransmittanceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:AtmosphericTransmittance>%s</tth:AtmosphericTransmittance>",
            onvif_format_float_num(p_req->AtmosphericTransmittance, 6, buff1, sizeof(buff1)-1));
    }

    if (p_req->ExtOpticsTemperatureFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:ExtOpticsTemperature>%s</tth:ExtOpticsTemperature>",
            onvif_format_float_num(p_req->ExtOpticsTemperature, 6, buff1, sizeof(buff1)-1));
    }

    if (p_req->ExtOpticsTransmittanceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tth:ExtOpticsTransmittance>%s</tth:ExtOpticsTransmittance>",
            onvif_format_float_num(p_req->ExtOpticsTransmittance, 6, buff1, sizeof(buff1)-1));
    }
    
    return offset;
}

int build_RadiometryConfiguration_xml(char * p_buf, int mlen, onvif_RadiometryConfiguration * p_req)
{
    int offset = 0;

    if (p_req->RadiometryGlobalParametersFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tth:RadiometryGlobalParameters>");
        offset += build_RadiometryGlobalParameters_xml(p_buf+offset, mlen-offset, &p_req->RadiometryGlobalParameters);
        offset += snprintf(p_buf+offset, mlen-offset, "</tth:RadiometryGlobalParameters>");
    }
    
    return offset;
}

int build_tth_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:GetServiceCapabilities />");
    return offset;
}

int build_tth_GetConfigurations_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;    
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:GetConfigurations />");
    return offset;
}

int build_tth_GetConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_GetConfiguration_REQ * p_req = (tth_GetConfiguration_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:GetConfiguration>"
            "<tth:VideoSourceToken>%s</tth:VideoSourceToken>"
        "</tth:GetConfiguration>",
        p_req->VideoSourceToken);

    return offset;
}

int build_tth_SetConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_SetConfiguration_REQ * p_req = (tth_SetConfiguration_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:SetConfiguration>");    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:VideoSourceToken>%s</tth:VideoSourceToken>",
        p_req->VideoSourceToken);
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:Configuration>");
    offset += build_ThermalConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, "</tth:Configuration>");        
    offset += snprintf(p_buf+offset, mlen-offset, "</tth:SetConfiguration>");

    return offset;
}


int build_tth_GetConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_GetConfigurationOptions_REQ * p_req = (tth_GetConfigurationOptions_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:GetConfigurationOptions>"
            "<tth:VideoSourceToken>%s</tth:VideoSourceToken>"
        "</tth:GetConfigurationOptions>",
        p_req->VideoSourceToken);

    return offset;
}

int build_tth_GetRadiometryConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_GetRadiometryConfiguration_REQ * p_req = (tth_GetRadiometryConfiguration_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:GetRadiometryConfiguration>"
            "<tth:VideoSourceToken>%s</tth:VideoSourceToken>"
        "</tth:GetRadiometryConfiguration>",
        p_req->VideoSourceToken);

    return offset;
}

int build_tth_SetRadiometryConfiguration_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_SetRadiometryConfiguration_REQ * p_req = (tth_SetRadiometryConfiguration_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:SetRadiometryConfiguration>");    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:VideoSourceToken>%s</tth:VideoSourceToken>",
        p_req->VideoSourceToken);
    offset += snprintf(p_buf+offset, mlen-offset, "<tth:Configuration>");
    offset += build_RadiometryConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, "</tth:Configuration>");        
    offset += snprintf(p_buf+offset, mlen-offset, "</tth:SetRadiometryConfiguration>");

    return offset;
}


int build_tth_GetRadiometryConfigurationOptions_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tth_GetRadiometryConfigurationOptions_REQ * p_req = (tth_GetRadiometryConfigurationOptions_REQ *) argv;
    assert(p_req);
        
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tth:GetRadiometryConfigurationOptions>"
            "<tth:VideoSourceToken>%s</tth:VideoSourceToken>"
        "</tth:GetRadiometryConfigurationOptions>",
        p_req->VideoSourceToken);

    return offset;
}

#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

int build_CredentialIdentifierType_xml(char * p_buf, int mlen, onvif_CredentialIdentifierType * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Name>%s</tcr:Name>"
        "<tcr:FormatType>%s</tcr:FormatType>",
        p_req->Name,
        p_req->FormatType);

    return offset;
}

int build_CredentialIdentifier_xml(char * p_buf, int mlen, onvif_CredentialIdentifier * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:Type>");
    offset += build_CredentialIdentifierType_xml(p_buf+offset, mlen-offset, &p_req->Type);   
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:Type>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:ExemptedFromAuthentication>%s</tcr:ExemptedFromAuthentication>"
        "<tcr:Value>%s</tcr:Value>",
        p_req->ExemptedFromAuthentication ? "true" : "false",
        p_req->Value);

    return offset;
}

int build_CredentialAccessProfile_xml(char * p_buf, int mlen, onvif_CredentialAccessProfile * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:AccessProfileToken>%s</tcr:AccessProfileToken>",
        p_req->AccessProfileToken);

    if (p_req->ValidFromFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:ValidFrom>%s</tcr:ValidFrom>", 
            p_req->ValidFrom);
    }

    if (p_req->ValidToFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:ValidTo>%s</tcr:ValidTo>", 
            p_req->ValidTo);
    }
    
    return offset;        
}

int build_Credential_xml(char * p_buf, int mlen, onvif_Credential * p_req)
{
    uint32 i;
    int offset = 0;

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Description>%s</tcr:Description>", 
            p_req->Description);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:CredentialHolderReference>%s</tcr:CredentialHolderReference>", 
            p_req->CredentialHolderReference);

    if (p_req->ValidFromFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:ValidFrom>%s</tcr:ValidFrom>", 
            p_req->ValidFrom);
    }

    if (p_req->ValidToFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:ValidTo>%s</tcr:ValidTo>", 
            p_req->ValidTo);
    }

    for (i = 0; i < p_req->sizeCredentialIdentifier; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialIdentifier>");
        offset += build_CredentialIdentifier_xml(p_buf+offset, mlen-offset, &p_req->CredentialIdentifier[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tcr:CredentialIdentifier>");
    }

    for (i = 0; i < p_req->sizeCredentialAccessProfile; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialAccessProfile>");
        offset += build_CredentialAccessProfile_xml(p_buf+offset, mlen-offset, &p_req->CredentialAccessProfile[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tcr:CredentialAccessProfile>");
    }

    for (i = 0; i < p_req->sizeAttribute; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Attribute  Name=\"%s\"",
            p_req->Attribute[i].Name);

        if (p_req->Attribute[i].ValueFlag)
        {
            offset += snprintf(p_buf+offset, mlen-offset, 
                " Value=\"%s\"",
                p_req->Attribute[i].Value);
        }

        offset += snprintf(p_buf+offset, mlen-offset, "/>");
    }
    
    return offset;
}

int build_CredentialState_xml(char * p_buf, int mlen, onvif_CredentialState * p_req)
{
    int offset = 0;
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Enabled>%s</tcr:Enabled>", 
        p_req->Enabled ? "true" : "false");

    if (p_req->ReasonFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Reason>%s</tcr:Reason>", 
            p_req->Reason);
    }
    
    if (p_req->AntipassbackStateFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:AntipassbackState>"
                "<tcr:AntipassbackViolated>%s</tcr:AntipassbackViolated>"
            "</tcr:AntipassbackState>", 
            p_req->AntipassbackState.AntipassbackViolated ? "true" : "false");
    }

    return offset;
}

int build_tcr_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:GetServiceCapabilities />");
    return offset;
}

int build_tcr_GetCredentialInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tcr_GetCredentialInfo_REQ * p_req = (tcr_GetCredentialInfo_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:GetCredentialInfo>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Token>%s</tcr:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:GetCredentialInfo>");

    return offset;    
}

int build_tcr_GetCredentialInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetCredentialInfoList_REQ * p_req = (tcr_GetCredentialInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:GetCredentialInfoList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Limit>%d</tcr:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:StartReference>%s</tcr:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:GetCredentialInfoList>");

    return offset;
}

int build_tcr_GetCredentials_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tcr_GetCredentials_REQ * p_req = (tcr_GetCredentials_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:GetCredentials>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Token>%s</tcr:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:GetCredentials>");

    return offset;
}

int build_tcr_GetCredentialList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetCredentialList_REQ * p_req = (tcr_GetCredentialList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:GetCredentialList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Limit>%d</tcr:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:StartReference>%s</tcr:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:GetCredentialList>");

    return offset;
}

int build_tcr_CreateCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_CreateCredential_REQ * p_req = (tcr_CreateCredential_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CreateCredential>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Credential token=\"%s\">", 
        p_req->Credential.token);
    offset += build_Credential_xml(p_buf+offset, mlen-offset, &p_req->Credential);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tcr:Credential>");

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:State>");
    offset += build_CredentialState_xml(p_buf+offset, mlen-offset, &p_req->State);    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:State>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:CreateCredential>");

    return offset;
}

int build_tcr_ModifyCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_ModifyCredential_REQ * p_req = (tcr_ModifyCredential_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:ModifyCredential>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Credential token=\"%s\">", 
        p_req->Credential.token);
    offset += build_Credential_xml(p_buf+offset, mlen-offset, &p_req->Credential);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tcr:Credential>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:ModifyCredential>");

    return offset;
}

int build_tcr_DeleteCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_DeleteCredential_REQ * p_req = (tcr_DeleteCredential_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:DeleteCredential>"
            "<tcr:Token>%s</tcr:Token>"
        "</tcr:DeleteCredential>",
        p_req->Token);

    return offset;    
}

int build_tcr_GetCredentialState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetCredentialState_REQ * p_req = (tcr_GetCredentialState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:GetCredentialState>"
            "<tcr:Token>%s</tcr:Token>"
        "</tcr:GetCredentialState>",
        p_req->Token);

    return offset;
}

int build_tcr_EnableCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_EnableCredential_REQ * p_req = (tcr_EnableCredential_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:EnableCredential>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Token>%s</tcr:Token>", 
        p_req->Token);

    if (p_req->ReasonFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Reason>%s</tcr:Reason>", 
            p_req->Reason);
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:EnableCredential>");

    return offset;
}

int build_tcr_DisableCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_DisableCredential_REQ * p_req = (tcr_DisableCredential_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:DisableCredential>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:Token>%s</tcr:Token>", 
        p_req->Token);

    if (p_req->ReasonFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:Reason>%s</tcr:Reason>", 
            p_req->Reason);
    }

    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:DisableCredential>");

    return offset;
}

int build_tcr_SetCredential_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_SetCredential_REQ * p_req = (tcr_SetCredential_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:SetCredential>");
    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialData>");

    offset += build_Credential_xml(p_buf+offset, mlen-offset, &p_req->CredentialData.Credential);

    offset += build_CredentialState_xml(p_buf+offset, mlen-offset, &p_req->CredentialData.CredentialState);

    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:CredentialData>");
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:SetCredential>");

    return offset;
}

int build_tcr_ResetAntipassbackViolation_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_ResetAntipassbackViolation_REQ * p_req = (tcr_ResetAntipassbackViolation_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:ResetAntipassbackViolation>"
            "<tcr:CredentialToken>%s</tcr:CredentialToken>"
        "</tcr:ResetAntipassbackViolation>",
        p_req->CredentialToken);

    return offset;
}

int build_tcr_GetSupportedFormatTypes_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetSupportedFormatTypes_REQ * p_req = (tcr_GetSupportedFormatTypes_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:GetSupportedFormatTypes>"
            "<tcr:CredentialIdentifierTypeName>%s</tcr:CredentialIdentifierTypeName>"
        "</tcr:GetSupportedFormatTypes>",
        p_req->CredentialIdentifierTypeName);

    return offset;
}

int build_tcr_GetCredentialIdentifiers_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetCredentialIdentifiers_REQ * p_req = (tcr_GetCredentialIdentifiers_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:GetCredentialIdentifiers>"
            "<tcr:CredentialToken>%s</tcr:CredentialToken>"
        "</tcr:GetCredentialIdentifiers>",
        p_req->CredentialToken);

    return offset;
}

int build_tcr_SetCredentialIdentifier_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_SetCredentialIdentifier_REQ * p_req = (tcr_SetCredentialIdentifier_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:SetCredentialIdentifier>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:CredentialToken>%s</tcr:CredentialToken>", 
        p_req->CredentialToken);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialIdentifier>");
    offset += build_CredentialIdentifier_xml(p_buf+offset, mlen-offset, &p_req->CredentialIdentifier);
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:CredentialIdentifier>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:SetCredentialIdentifier>");

    return offset;
}

int build_tcr_DeleteCredentialIdentifier_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_DeleteCredentialIdentifier_REQ * p_req = (tcr_DeleteCredentialIdentifier_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:DeleteCredentialIdentifier>"
            "<tcr:CredentialToken>%s</tcr:CredentialToken>"
            "<tcr:CredentialIdentifierTypeName>%s</tcr:CredentialIdentifierTypeName>"
        "</tcr:DeleteCredentialIdentifier>",
        p_req->CredentialToken,
        p_req->CredentialIdentifierTypeName);

    return offset;
}

int build_tcr_GetCredentialAccessProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tcr_GetCredentialAccessProfiles_REQ * p_req = (tcr_GetCredentialAccessProfiles_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:GetCredentialAccessProfiles>"
            "<tcr:CredentialToken>%s</tcr:CredentialToken>"
        "</tcr:GetCredentialAccessProfiles>",
        p_req->CredentialToken);

    return offset;
}

int build_tcr_SetCredentialAccessProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tcr_SetCredentialAccessProfiles_REQ * p_req = (tcr_SetCredentialAccessProfiles_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tcr:SetCredentialAccessProfiles>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:CredentialToken>%s</tcr:CredentialToken>", 
        p_req->CredentialToken);

    for (i = 0; i < p_req->sizeCredentialAccessProfile; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialAccessProfile>");
        offset += build_CredentialAccessProfile_xml(p_buf+offset, mlen-offset, &p_req->CredentialAccessProfile[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "<tcr:CredentialAccessProfile>");
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tcr:SetCredentialAccessProfiles>");

    return offset;
}

int build_tcr_DeleteCredentialAccessProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tcr_DeleteCredentialAccessProfiles_REQ * p_req = (tcr_DeleteCredentialAccessProfiles_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:DeleteCredentialAccessProfiles>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tcr:CredentialToken>%s</tcr:CredentialToken>",
        p_req->CredentialToken);

    for (i = 0; i < p_req->sizeAccessProfileToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tcr:AccessProfileToken>%s</tcr:AccessProfileToken>",
            p_req->AccessProfileToken[i]);
    }
    
    offset += snprintf(p_buf + offset, mlen - offset, 
        "</tcr:DeleteCredentialAccessProfiles>");

    return offset;
}

#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

int build_AccessPolicy_xml(char * p_buf, int mlen, onvif_AccessPolicy * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tar:ScheduleToken>%s</tar:ScheduleToken>"
        "<tar:Entity>%s</tar:Entity>", 
        p_req->ScheduleToken,
        p_req->Entity);

    if (p_req->EntityTypeFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:EntityType>%s</tar:EntityType>",
            p_req->EntityType);
    }

    return offset;
}

int build_AccessProfile_xml(char * p_buf, int mlen, onvif_AccessProfile * p_req)
{
    uint32 i;
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tar:Name>%s</tar:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:Description>%s</tar:Description>", 
            p_req->Description);
    }

    for (i = 0; i < p_req->sizeAccessPolicy; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tar:AccessPolicy>");
        offset += build_AccessPolicy_xml(p_buf+offset, mlen-offset, &p_req->AccessPolicy[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tar:AccessPolicy>");
    }

    return offset;
}

int build_tar_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tar:GetServiceCapabilities />");
    return offset;
}

int build_tar_GetAccessProfileInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tar_GetAccessProfileInfo_REQ * p_req = (tar_GetAccessProfileInfo_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:GetAccessProfileInfo>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:Token>%s</tar:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:GetAccessProfileInfo>");

    return offset; 
}

int build_tar_GetAccessProfileInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tar_GetAccessProfileInfoList_REQ * p_req = (tar_GetAccessProfileInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:GetAccessProfileInfoList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:Limit>%d</tar:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:StartReference>%s</tar:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:GetAccessProfileInfoList>");

    return offset;
}

int build_tar_GetAccessProfiles_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tar_GetAccessProfiles_REQ * p_req = (tar_GetAccessProfiles_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:GetAccessProfiles>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:Token>%s</tar:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:GetAccessProfiles>");

    return offset;
}

int build_tar_GetAccessProfileList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tar_GetAccessProfileList_REQ * p_req = (tar_GetAccessProfileList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:GetAccessProfileList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:Limit>%d</tar:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tar:StartReference>%s</tar:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:GetAccessProfileList>");

    return offset;
}

int build_tar_CreateAccessProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tar_CreateAccessProfile_REQ * p_req = (tar_CreateAccessProfile_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:CreateAccessProfile>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tar:AccessProfile token=\"%s\">", 
        p_req->AccessProfile.token);
    offset += build_AccessProfile_xml(p_buf+offset, mlen-offset, &p_req->AccessProfile);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tar:AccessProfile>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:CreateAccessProfile>");

    return offset;
}

int build_tar_ModifyAccessProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tar_ModifyAccessProfile_REQ * p_req = (tar_ModifyAccessProfile_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tar:ModifyAccessProfile>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tar:AccessProfile token=\"%s\">", 
        p_req->AccessProfile.token);
    offset += build_AccessProfile_xml(p_buf+offset, mlen-offset, &p_req->AccessProfile);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tar:AccessProfile>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tar:ModifyAccessProfile>");

    return offset;
}

int build_tar_DeleteAccessProfile_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tar_DeleteAccessProfile_REQ * p_req = (tar_DeleteAccessProfile_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tar:DeleteAccessProfile>"
            "<tar:Token>%s</tar:Token>"
        "</tar:DeleteAccessProfile>",
        p_req->Token);

    return offset;
}

#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

int build_TimePeriod_xml(char * p_buf, int mlen, onvif_TimePeriod * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:From>%s</tsc:From>", 
        p_req->From);

    if (p_req->UntilFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Until>%s</tsc:Until>", 
            p_req->Until);
    }
    
    return offset;
}

int build_SpecialDaysSchedule_xml(char * p_buf, int mlen, onvif_SpecialDaysSchedule * p_req)
{
    uint32 i;
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:GroupToken>%s</tsc:GroupToken>", 
        p_req->GroupToken);

    for (i = 0; i < p_req->sizeTimeRange; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tsc:TimeRange>");
        offset += build_TimePeriod_xml(p_buf+offset, mlen-offset, &p_req->TimeRange[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tsc:TimeRange>");
    }
    
    return offset;
}

int build_Schedule_xml(char * p_buf, int mlen, onvif_Schedule * p_req)
{
    uint32 i;
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:Name>%s</tsc:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Description>%s</tsc:Description>", 
            p_req->Description);
    }

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:Standard>%s</tsc:Standard>", 
        p_req->Standard);

    for (i = 0; i < p_req->sizeSpecialDays; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, "<tsc:SpecialDays>");
        offset += build_SpecialDaysSchedule_xml(p_buf+offset, mlen-offset, &p_req->SpecialDays[i]);
        offset += snprintf(p_buf+offset, mlen-offset, "</tsc:SpecialDays>");
    }
    
    return offset;
}

int build_SpecialDayGroup_xml(char * p_buf, int mlen, onvif_SpecialDayGroup * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:Name>%s</tsc:Name>", 
        p_req->Name);

    if (p_req->DescriptionFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Description>%s</tsc:Description>", 
            p_req->Description);
    }

    if (p_req->DaysFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Days>%s</tsc:Days>", 
            p_req->Days);
    }

    return offset;
}

int build_tsc_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetServiceCapabilities />");
    return offset;
}

int build_tsc_GetScheduleInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tsc_GetScheduleInfo_REQ * p_req = (tsc_GetScheduleInfo_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetScheduleInfo>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Token>%s</tsc:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetScheduleInfo>");

    return offset; 
}

int build_tsc_GetScheduleInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_GetScheduleInfoList_REQ * p_req = (tsc_GetScheduleInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetScheduleInfoList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Limit>%d</tsc:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:StartReference>%s</tsc:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetScheduleInfoList>");

    return offset;
}

int build_tsc_GetSchedules_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tsc_GetSchedules_REQ * p_req = (tsc_GetSchedules_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetSchedules>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Token>%s</tsc:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetSchedules>");

    return offset;
}

int build_tsc_GetScheduleList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_GetScheduleList_REQ * p_req = (tsc_GetScheduleList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetScheduleList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Limit>%d</tsc:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:StartReference>%s</tsc:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetScheduleList>");

    return offset;
}

int build_tsc_CreateSchedule_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_CreateSchedule_REQ * p_req = (tsc_CreateSchedule_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:CreateSchedule>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:Schedule token=\"%s\">", 
        p_req->Schedule.token);
    offset += build_Schedule_xml(p_buf+offset, mlen-offset, &p_req->Schedule);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tsc:Schedule>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:CreateSchedule>");

    return offset;
}

int build_tsc_ModifySchedule_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_ModifySchedule_REQ * p_req = (tsc_ModifySchedule_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:ModifySchedule>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:Schedule token=\"%s\">", 
        p_req->Schedule.token);
    offset += build_Schedule_xml(p_buf+offset, mlen-offset, &p_req->Schedule);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tsc:Schedule>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:ModifySchedule>");

    return offset;
}

int build_tsc_DeleteSchedule_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_DeleteSchedule_REQ * p_req = (tsc_DeleteSchedule_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:DeleteSchedule>"
            "<tsc:Token>%s</tsc:Token>"
        "</tsc:DeleteSchedule>",
        p_req->Token);

    return offset;
}

int build_tsc_GetSpecialDayGroupInfo_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tsc_GetSpecialDayGroupInfo_REQ * p_req = (tsc_GetSpecialDayGroupInfo_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetSpecialDayGroupInfo>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Token>%s</tsc:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetSpecialDayGroupInfo>");

    return offset;
}

int build_tsc_GetSpecialDayGroupInfoList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_GetSpecialDayGroupInfoList_REQ * p_req = (tsc_GetSpecialDayGroupInfoList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetSpecialDayGroupInfoList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Limit>%d</tsc:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:StartReference>%s</tsc:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetSpecialDayGroupInfoList>");

    return offset;
}

int build_tsc_GetSpecialDayGroups_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int i;
    int offset = 0;
    tsc_GetSpecialDayGroups_REQ * p_req = (tsc_GetSpecialDayGroups_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetSpecialDayGroups>");

    for (i = 0; i < p_req->sizeToken; i++)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Token>%s</tsc:Token>", 
            p_req->Token[i]);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetSpecialDayGroups>");

    return offset;
}

int build_tsc_GetSpecialDayGroupList_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_GetSpecialDayGroupList_REQ * p_req = (tsc_GetSpecialDayGroupList_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:GetSpecialDayGroupList>");

    if (p_req->LimitFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:Limit>%d</tsc:Limit>", 
            p_req->Limit);
    }

    if (p_req->StartReferenceFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tsc:StartReference>%s</tsc:StartReference>", 
            p_req->StartReference);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:GetSpecialDayGroupList>");

    return offset;
}

int build_tsc_CreateSpecialDayGroup_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_CreateSpecialDayGroup_REQ * p_req = (tsc_CreateSpecialDayGroup_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:CreateSpecialDayGroup>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:SpecialDayGroup token=\"%s\">", 
        p_req->SpecialDayGroup.token);
    offset += build_SpecialDayGroup_xml(p_buf+offset, mlen-offset, &p_req->SpecialDayGroup);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tsc:SpecialDayGroup>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:CreateSpecialDayGroup>");

    return offset;
}

int build_tsc_ModifySpecialDayGroup_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_ModifySpecialDayGroup_REQ * p_req = (tsc_ModifySpecialDayGroup_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<tsc:ModifySpecialDayGroup>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:SpecialDayGroup token=\"%s\">", 
        p_req->SpecialDayGroup.token);
    offset += build_SpecialDayGroup_xml(p_buf+offset, mlen-offset, &p_req->SpecialDayGroup);
    offset += snprintf(p_buf+offset, mlen-offset, 
        "</tsc:SpecialDayGroup>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tsc:ModifySpecialDayGroup>");

    return offset;
}

int build_tsc_DeleteSpecialDayGroup_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_DeleteSpecialDayGroup_REQ * p_req = (tsc_DeleteSpecialDayGroup_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:DeleteSpecialDayGroup>"
            "<tsc:Token>%s</tsc:Token>"
        "</tsc:DeleteSpecialDayGroup>",
        p_req->Token);

    return offset;
}

int build_tsc_GetScheduleState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tsc_GetScheduleState_REQ * p_req = (tsc_GetScheduleState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tsc:GetScheduleState>"
            "<tsc:Token>%s</tsc:Token>"
        "</tsc:GetScheduleState>",
        p_req->Token);

    return offset;
}

#endif // end of SCHEDULE_SUPPORT

#ifdef RECEIVER_SUPPORT

int build_trv_ReceiverConfiguration_xml(char * p_buf, int mlen, onvif_ReceiverConfiguration * p_req)
{
    int offset = 0;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tt:Mode>%s</tt:Mode>"
        "<tt:MediaUri>%s</tt:MediaUri>", 
        onvif_ReceiverModeToString(p_req->Mode), 
        p_req->MediaUri);

    offset += snprintf(p_buf+offset, mlen-offset, "<tt:StreamSetup>");
    offset += build_StreamSetup_xml(p_buf+offset, mlen-offset, &p_req->StreamSetup);
    offset += snprintf(p_buf+offset, mlen-offset, "</tt:StreamSetup>");
    
    return offset;        
}

int build_trv_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trv:GetServiceCapabilities />");
    return offset;
}

int build_trv_GetReceivers_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<trv:GetReceivers />");
    return offset;
}

int build_trv_GetReceiver_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trv_GetReceiver_REQ * p_req = (trv_GetReceiver_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trv:GetReceiver>"
            "<trv:ReceiverToken>%s</trv:ReceiverToken>"
        "</trv:GetReceiver>",
        p_req->ReceiverToken);
        
    return offset;
}

int build_trv_CreateReceiver_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{    
    int offset = 0;
    trv_CreateReceiver_REQ * p_req = (trv_CreateReceiver_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<trv:CreateReceiver>");

    offset += snprintf(p_buf+offset, mlen-offset, "<trv:Configuration>");
    offset += build_trv_ReceiverConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trv:Configuration>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</trv:CreateReceiver>");

    return offset;
}

int build_trv_DeleteReceiver_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trv_DeleteReceiver_REQ * p_req = (trv_DeleteReceiver_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trv:DeleteReceiver>"
            "<trv:ReceiverToken>%s</trv:ReceiverToken>"
        "</trv:DeleteReceiver>",
        p_req->ReceiverToken);

    return offset;
}

int build_trv_ConfigureReceiver_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trv_ConfigureReceiver_REQ * p_req = (trv_ConfigureReceiver_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, "<trv:ConfigureReceiver>");
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trv:ReceiverToken>%s</trv:ReceiverToken>",
        p_req->ReceiverToken);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<trv:Configuration>");
    offset += build_trv_ReceiverConfiguration_xml(p_buf+offset, mlen-offset, &p_req->Configuration);
    offset += snprintf(p_buf+offset, mlen-offset, "</trv:Configuration>");
    
    offset += snprintf(p_buf+offset, mlen-offset, "</trv:ConfigureReceiver>");

    return offset;
}

int build_trv_SetReceiverMode_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trv_SetReceiverMode_REQ * p_req = (trv_SetReceiverMode_REQ *) argv;

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trv:SetReceiverMode>"
            "<trv:ReceiverToken>%s</trv:ReceiverToken>"
            "<trv:Mode>%s</trv:Mode>"
        "</trv:SetReceiverMode>",
        p_req->ReceiverToken,
        onvif_ReceiverModeToString(p_req->Mode));

    return offset;
}

int build_trv_GetReceiverState_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    trv_GetReceiverState_REQ * p_req = (trv_GetReceiverState_REQ *) argv;
    assert(p_req);

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<trv:GetReceiverState>"
            "<trv:ReceiverToken>%s</trv:ReceiverToken>"
        "</trv:GetReceiverState>",
        p_req->ReceiverToken);

    return offset;
}

#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

int build_tpv_GetServiceCapabilities_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:GetServiceCapabilities />");
    return offset;
}

int build_tpv_PanMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_PanMove_REQ * p_req = (tpv_PanMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:PanMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:VideoSource>%s</tpv:VideoSource>"
        "<tpv:Direction>%s</tpv:Direction>",
        p_req->VideoSource,
        onvif_PanDirectionToString(p_req->Direction));

    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tpv:Timeout>PT%dS</tpv:Timeout>",
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tpv:PanMove>");
    
    return offset;
}

int build_tpv_TiltMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_TiltMove_REQ * p_req = (tpv_TiltMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:TiltMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:VideoSource>%s</tpv:VideoSource>"
        "<tpv:Direction>%s</tpv:Direction>",
        p_req->VideoSource,
        onvif_TiltDirectionToString(p_req->Direction));

    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tpv:Timeout>PT%dS</tpv:Timeout>",
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tpv:TiltMove>");
    
    return offset;
}

int build_tpv_ZoomMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_ZoomMove_REQ * p_req = (tpv_ZoomMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:ZoomMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:VideoSource>%s</tpv:VideoSource>"
        "<tpv:Direction>%s</tpv:Direction>",
        p_req->VideoSource,
        onvif_ZoomDirectionToString(p_req->Direction));

    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tpv:Timeout>PT%dS</tpv:Timeout>",
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tpv:ZoomMove>");
    
    return offset;
}

int build_tpv_RollMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_RollMove_REQ * p_req = (tpv_RollMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:RollMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:VideoSource>%s</tpv:VideoSource>"
        "<tpv:Direction>%s</tpv:Direction>",
        p_req->VideoSource,
        onvif_RollDirectionToString(p_req->Direction));

    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tpv:Timeout>PT%dS</tpv:Timeout>",
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tpv:RollMove>");
    
    return offset;
}

int build_tpv_FocusMove_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_FocusMove_REQ * p_req = (tpv_FocusMove_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, "<tpv:FocusMove>");

    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:VideoSource>%s</tpv:VideoSource>"
        "<tpv:Direction>%s</tpv:Direction>",
        p_req->VideoSource,
        onvif_FocusDirectionToString(p_req->Direction));

    if (p_req->TimeoutFlag)
    {
        offset += snprintf(p_buf+offset, mlen-offset, 
            "<tpv:Timeout>PT%dS</tpv:Timeout>",
            p_req->Timeout);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "</tpv:FocusMove>");
    
    return offset;
}

int build_tpv_Stop_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_Stop_REQ * p_req = (tpv_Stop_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:Stop>"
            "<tpv:VideoSource>%s</tpv:VideoSource>"
        "</tpv:Stop>",
        p_req->VideoSource);
    
    return offset;
}

int build_tpv_GetUsage_xml(char * p_buf, int mlen, ONVIF_DEVICE * p_dev, void * argv)
{
    int offset = 0;
    tpv_GetUsage_REQ * p_req = (tpv_GetUsage_REQ *) argv;
    assert(p_req);
    
    offset += snprintf(p_buf+offset, mlen-offset, 
        "<tpv:GetUsage>"
            "<tpv:VideoSource>%s</tpv:VideoSource>"
        "</tpv:GetUsage>",
        p_req->VideoSource);
    
    return offset;
}

#endif // end of PROVISIONING_SUPPORT

int build_onvif_req_xml(char * p_buf, int mlen, eOnvifAction type, ONVIF_DEVICE * p_dev, void * p_req)
{
    int rlen = 0;
    int offset = snprintf(p_buf, mlen, "%s", xml_hdr);

    offset += snprintf(p_buf+offset, mlen-offset, "%s", onvif_xmlns);

    if (type == etevRenew)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevUnsubscribe)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevPauseSubscription)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/PausableSubscriptionManager/PauseSubscriptionRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevResumeSubscription)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/PausableSubscriptionManager/ResumeSubscriptionRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevDestroyPullPoint)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/PullPoint/DestroyPullPointRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevPullMessages)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevGetMessages)
    {
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, p_dev->events.producter_addr, 
            "http://docs.oasis-open.org/wsn/bw-2/PullPoint/GetMessagesRequest", 
            p_dev->events.producter_parameters);
    }
    else if (type == etevSubscribe)
    {
        char to[512];
        snprintf(to, sizeof(to), 
            "http://%s:%d%s", 
            p_dev->Capabilities.events.XAddr.host, 
            p_dev->Capabilities.events.XAddr.port, 
            p_dev->Capabilities.events.XAddr.url);
    
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, to, 
            "http://docs.oasis-open.org/wsn/bw-2/NotificationProducer/SubscribeRequest", 
            NULL);
    }
    else if (type == etevCreatePullPointSubscription)
    {
        char to[512];
        snprintf(to, sizeof(to), 
            "http://%s:%d%s", 
            p_dev->Capabilities.events.XAddr.host, 
            p_dev->Capabilities.events.XAddr.port, 
            p_dev->Capabilities.events.XAddr.url);
    
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, to, 
            "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionRequest", 
            NULL);
    }
    else if (type == etevSeek)
    {
        char to[512];
        snprintf(to, sizeof(to), 
            "http://%s:%d%s", 
            p_dev->Capabilities.events.XAddr.host, 
            p_dev->Capabilities.events.XAddr.port, 
            p_dev->Capabilities.events.XAddr.url);
    
        offset += build_onvif_req_header_ex(p_buf+offset, mlen-offset, 
            p_dev, to, 
            "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SeekRequest", 
            NULL);
    }
    else
    {
        offset += build_onvif_req_header(p_buf+offset, mlen-offset, p_dev, NULL, type);
    }
    
    offset += snprintf(p_buf+offset, mlen-offset, "%s", soap_body);
    
    switch (type)
    {
    // onvif device service interfaces
    
    case etdsGetCapabilities:
        rlen = build_tds_GetCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetServices:
        rlen = build_tds_GetServices_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetServiceCapabilities:
        rlen = build_tds_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetDeviceInformation:
        rlen = build_tds_GetDeviceInformation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetUsers:
        rlen = build_tds_GetUsers_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsCreateUsers:
        rlen = build_tds_CreateUsers_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsDeleteUsers:
        rlen = build_tds_DeleteUsers_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetUser:
        rlen = build_tds_SetUser_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetRemoteUser:
        rlen = build_tds_GetRemoteUser_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetRemoteUser:
        rlen = build_tds_SetRemoteUser_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetNetworkInterfaces:
        rlen = build_tds_GetNetworkInterfaces_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetNetworkInterfaces:
        rlen = build_tds_SetNetworkInterfaces_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetNTP:
        rlen = build_tds_GetNTP_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetNTP:
        rlen = build_tds_SetNTP_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetHostname:
        rlen = build_tds_GetHostname_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetHostname:
        rlen = build_tds_SetHostname_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetHostnameFromDHCP:
        rlen = build_tds_SetHostnameFromDHCP_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetDNS:
        rlen = build_tds_GetDNS_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetDNS:
        rlen = build_tds_SetDNS_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;    
        
    case etdsGetDynamicDNS:
        rlen = build_tds_GetDynamicDNS_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetDynamicDNS:
        rlen = build_tds_SetDynamicDNS_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetNetworkProtocols:
        rlen = build_tds_GetNetworkProtocols_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetNetworkProtocols:
        rlen = build_tds_SetNetworkProtocols_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetDiscoveryMode:
        rlen = build_tds_GetDiscoveryMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetDiscoveryMode:
        rlen = build_tds_SetDiscoveryMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetNetworkDefaultGateway:
        rlen = build_tds_GetNetworkDefaultGateway_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetNetworkDefaultGateway:
        rlen = build_tds_SetNetworkDefaultGateway_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetZeroConfiguration:
        rlen = build_tds_GetZeroConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetZeroConfiguration:
        rlen = build_tds_SetZeroConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetEndpointReference:
        rlen = build_tds_GetEndpointReference_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSendAuxiliaryCommand:
        rlen = build_tds_SendAuxiliaryCommand_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetRelayOutputs:
        rlen = build_tds_GetRelayOutputs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetRelayOutputSettings:
        rlen = build_tds_SetRelayOutputSettings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetRelayOutputState:
        rlen = build_tds_SetRelayOutputState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetSystemDateAndTime:
        rlen = build_tds_GetSystemDateAndTime_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetSystemDateAndTime:
        rlen = build_tds_SetSystemDateAndTime_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSystemReboot:
        rlen = build_tds_SystemReboot_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetSystemFactoryDefault:
        rlen = build_tds_SetSystemFactoryDefault_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetSystemLog:
        rlen = build_tds_GetSystemLog_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetScopes:
        rlen = build_tds_GetScopes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetScopes:
        rlen = build_tds_SetScopes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsAddScopes:
        rlen = build_tds_AddScopes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsRemoveScopes:
        rlen = build_tds_RemoveScopes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsStartFirmwareUpgrade:
        rlen = build_tds_StartFirmwareUpgrade_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetSystemUris:
        rlen = build_tds_GetSystemUris_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsStartSystemRestore:
        rlen = build_tds_StartSystemRestore_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsGetWsdlUrl:
        rlen = build_tds_GetWsdlUrl_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetDot11Capabilities:
        rlen = build_tds_GetDot11Capabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetDot11Status:
        rlen = build_tds_GetDot11Status_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsScanAvailableDot11Networks:
        rlen = build_tds_ScanAvailableDot11Networks_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetGeoLocation:
        rlen = build_tds_GetGeoLocation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetGeoLocation:
        rlen = build_tds_SetGeoLocation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsDeleteGeoLocation:
        rlen = build_tds_DeleteGeoLocation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdsSetHashingAlgorithm:
        rlen = build_tds_SetHashingAlgorithm_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#ifdef IPFILTER_SUPPORT
    case etdsGetIPAddressFilter:
        rlen = build_tds_GetIPAddressFilter_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetIPAddressFilter:
        rlen = build_tds_SetIPAddressFilter_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsAddIPAddressFilter:
        rlen = build_tds_AddIPAddressFilter_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsRemoveIPAddressFilter:
        rlen = build_tds_RemoveIPAddressFilter_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
#endif // end of IPFILTER_SUPPORT

    case etdsGetAccessPolicy:
        rlen = build_tds_GetAccessPolicy_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetAccessPolicy:
        rlen = build_tds_SetAccessPolicy_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetStorageConfigurations:
        rlen = build_tds_GetStorageConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsCreateStorageConfiguration:
        rlen = build_tds_CreateStorageConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsGetStorageConfiguration:
        rlen = build_tds_GetStorageConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsSetStorageConfiguration:
        rlen = build_tds_SetStorageConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdsDeleteStorageConfiguration:
        rlen = build_tds_DeleteStorageConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    // onvif media service interfaces

    case etrtGetServiceCapabilities:
        rlen = build_trt_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoSources:
        rlen = build_trt_GetVideoSources_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioSources:
        rlen = build_trt_GetAudioSources_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtCreateProfile:
        rlen = build_trt_CreateProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetProfile:
        rlen = build_trt_GetProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetProfiles:
        rlen = build_trt_GetProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddVideoEncoderConfiguration:
        rlen = build_trt_AddVideoEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddVideoSourceConfiguration:
        rlen = build_trt_AddVideoSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddAudioEncoderConfiguration:
        rlen = build_trt_AddAudioEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddAudioSourceConfiguration:
        rlen = build_trt_AddAudioSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetVideoSourceModes:
        rlen = build_trt_GetVideoSourceModes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtSetVideoSourceMode:
        rlen = build_trt_SetVideoSourceMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddPTZConfiguration:
        rlen = build_trt_AddPTZConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveVideoEncoderConfiguration:
        rlen = build_trt_RemoveVideoEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveVideoSourceConfiguration:
        rlen = build_trt_RemoveVideoSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveAudioEncoderConfiguration:
        rlen = build_trt_RemoveAudioEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveAudioSourceConfiguration:
        rlen = build_trt_RemoveAudioSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemovePTZConfiguration:
        rlen = build_trt_RemovePTZConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtDeleteProfile:
        rlen = build_trt_DeleteProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoSourceConfigurations:
        rlen = build_trt_GetVideoSourceConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoEncoderConfigurations:
        rlen = build_trt_GetVideoEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioSourceConfigurations:
        rlen = build_trt_GetAudioSourceConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioEncoderConfigurations:
        rlen = build_trt_GetAudioEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoSourceConfiguration:
        rlen = build_trt_GetVideoSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoEncoderConfiguration:
        rlen = build_trt_GetVideoEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioSourceConfiguration:
        rlen = build_trt_GetAudioSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioEncoderConfiguration:
        rlen = build_trt_GetAudioEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetVideoSourceConfiguration:
        rlen = build_trt_SetVideoSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetVideoEncoderConfiguration:
        rlen = build_trt_SetVideoEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetAudioSourceConfiguration:
        rlen = build_trt_SetAudioSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetAudioEncoderConfiguration:
        rlen = build_trt_SetAudioEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoSourceConfigurationOptions:
        rlen = build_trt_GetVideoSourceConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoEncoderConfigurationOptions:
        rlen = build_trt_GetVideoEncoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioSourceConfigurationOptions:
        rlen = build_trt_GetAudioSourceConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioEncoderConfigurationOptions:
        rlen = build_trt_GetAudioEncoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetStreamUri:
        rlen = build_trt_GetStreamUri_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetSynchronizationPoint:
        rlen = build_trt_SetSynchronizationPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetSnapshotUri:
        rlen = build_trt_GetSnapshotUri_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetGuaranteedNumberOfVideoEncoderInstances:
        rlen = build_trt_GetGuaranteedNumberOfVideoEncoderInstances_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioOutputs:
        rlen = build_trt_GetAudioOutputs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioOutputConfigurations:
        rlen = build_trt_GetAudioOutputConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioOutputConfiguration:
        rlen = build_trt_GetAudioOutputConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioOutputConfigurationOptions:
        rlen = build_trt_GetAudioOutputConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetAudioOutputConfiguration:
        rlen = build_trt_SetAudioOutputConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioDecoderConfigurations:
        rlen = build_trt_GetAudioDecoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioDecoderConfiguration:
        rlen = build_trt_GetAudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetAudioDecoderConfigurationOptions:
        rlen = build_trt_GetAudioDecoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetAudioDecoderConfiguration:
        rlen = build_trt_SetAudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddAudioOutputConfiguration:
        rlen = build_trt_AddAudioOutputConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddAudioDecoderConfiguration:
        rlen = build_trt_AddAudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveAudioOutputConfiguration:
        rlen = build_trt_RemoveAudioOutputConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveAudioDecoderConfiguration:
        rlen = build_trt_RemoveAudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetOSDs:
        rlen = build_trt_GetOSDs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetOSD:
        rlen = build_trt_GetOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetOSD:
        rlen = build_trt_SetOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetOSDOptions:
        rlen = build_trt_GetOSDOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtCreateOSD:
        rlen = build_trt_CreateOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtDeleteOSD:
        rlen = build_trt_DeleteOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetVideoAnalyticsConfigurations:
        rlen = build_trt_GetVideoAnalyticsConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddVideoAnalyticsConfiguration:
        rlen = build_trt_AddVideoAnalyticsConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetVideoAnalyticsConfiguration:
        rlen = build_trt_GetVideoAnalyticsConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveVideoAnalyticsConfiguration:
        rlen = build_trt_RemoveVideoAnalyticsConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetVideoAnalyticsConfiguration:
        rlen = build_trt_SetVideoAnalyticsConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetMetadataConfigurations:
        rlen = build_trt_GetMetadataConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtAddMetadataConfiguration:
        rlen = build_trt_AddMetadataConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetMetadataConfiguration:
        rlen = build_trt_GetMetadataConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtRemoveMetadataConfiguration:
        rlen = build_trt_RemoveMetadataConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtSetMetadataConfiguration:
        rlen = build_trt_SetMetadataConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetMetadataConfigurationOptions:
        rlen = build_trt_GetMetadataConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrtGetCompatibleVideoEncoderConfigurations:
        rlen = build_trt_GetCompatibleVideoEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetCompatibleAudioEncoderConfigurations:
        rlen = build_trt_GetCompatibleAudioEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetCompatibleVideoAnalyticsConfigurations:
        rlen = build_trt_GetCompatibleVideoAnalyticsConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrtGetCompatibleMetadataConfigurations:
        rlen = build_trt_GetCompatibleMetadataConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    // onvif media 2 service interfaces

    case etr2GetServiceCapabilities:
        rlen = build_tr2_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoEncoderConfigurations:
        rlen = build_tr2_GetVideoEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetVideoEncoderConfiguration:
        rlen = build_tr2_SetVideoEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoEncoderConfigurationOptions:
        rlen = build_tr2_GetVideoEncoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etr2GetProfiles:
        rlen = build_tr2_GetProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2CreateProfile:
        rlen = build_tr2_CreateProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2DeleteProfile:
        rlen = build_tr2_DeleteProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetStreamUri:
        rlen = build_tr2_GetStreamUri_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoSourceConfigurations:
        rlen = build_tr2_GetVideoSourceConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoSourceConfigurationOptions:
        rlen = build_tr2_GetVideoSourceConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetVideoSourceConfiguration:
        rlen = build_tr2_SetVideoSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetSynchronizationPoint:
        rlen = build_tr2_SetSynchronizationPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetMetadataConfigurations:
        rlen = build_tr2_GetMetadataConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetMetadataConfigurationOptions:
        rlen = build_tr2_GetMetadataConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetMetadataConfiguration:
        rlen = build_tr2_SetMetadataConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioEncoderConfigurations:
        rlen = build_tr2_GetAudioEncoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioSourceConfigurations:
        rlen = build_tr2_GetAudioSourceConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioSourceConfigurationOptions:
        rlen = build_tr2_GetAudioSourceConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetAudioSourceConfiguration:
        rlen = build_tr2_SetAudioSourceConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetAudioEncoderConfiguration:
        rlen = build_tr2_SetAudioEncoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioEncoderConfigurationOptions:
        rlen = build_tr2_GetAudioEncoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2AddConfiguration:
        rlen = build_tr2_AddConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2RemoveConfiguration:
        rlen = build_tr2_RemoveConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoEncoderInstances:
        rlen = build_tr2_GetVideoEncoderInstances_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;        

    case etr2GetAudioOutputConfigurations:
        rlen = build_tr2_GetAudioOutputConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioOutputConfigurationOptions:
        rlen = build_tr2_GetAudioOutputConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetAudioOutputConfiguration:
        rlen = build_tr2_SetAudioOutputConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioDecoderConfigurations:
        rlen = build_tr2_GetAudioDecoderConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetAudioDecoderConfigurationOptions:
        rlen = build_tr2_GetAudioDecoderConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetAudioDecoderConfiguration:
        rlen = build_tr2_SetAudioDecoderConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetSnapshotUri:
        rlen = build_tr2_GetSnapshotUri_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2StartMulticastStreaming:
        rlen = build_tr2_StartMulticastStreaming_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2StopMulticastStreaming:
        rlen = build_tr2_StopMulticastStreaming_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetVideoSourceModes:
        rlen = build_tr2_GetVideoSourceModes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetVideoSourceMode:
        rlen = build_tr2_SetVideoSourceMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2CreateOSD:
        rlen = build_tr2_CreateOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2DeleteOSD:
        rlen = build_tr2_DeleteOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetOSDs:
        rlen = build_tr2_GetOSDs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetOSD:
        rlen = build_tr2_SetOSD_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetOSDOptions:
        rlen = build_tr2_GetOSDOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etr2GetAnalyticsConfigurations:
        rlen = build_tr2_GetAnalyticsConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etr2GetMasks:
        rlen = build_tr2_GetMasks_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2SetMask:
        rlen = build_tr2_SetMask_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2CreateMask:
        rlen = build_tr2_CreateMask_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2DeleteMask:
        rlen = build_tr2_DeleteMask_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etr2GetMaskOptions:
        rlen = build_tr2_GetMaskOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    // onvif ptz service interfaces

    case eptzGetServiceCapabilities:
        rlen = build_ptz_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetNodes:
        rlen = build_ptz_GetNodes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetNode:
        rlen = build_ptz_GetNode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetPresets:
        rlen = build_ptz_GetPresets_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzSetPreset:
        rlen = build_ptz_SetPreset_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzRemovePreset:
        rlen = build_ptz_RemovePreset_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGotoPreset:
        rlen = build_ptz_GotoPreset_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGotoHomePosition:
        rlen = build_ptz_GotoHomePosition_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzSetHomePosition:
        rlen = build_ptz_SetHomePosition_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetStatus:
        rlen = build_ptz_GetStatus_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzContinuousMove:
        rlen = build_ptz_ContinuousMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzRelativeMove:
        rlen = build_ptz_RelativeMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzAbsoluteMove:
        rlen = build_ptz_AbsoluteMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzStop:
        rlen = build_ptz_Stop_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetConfigurations:
        rlen = build_ptz_GetConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetConfiguration:
        rlen = build_ptz_GetConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzSetConfiguration:
        rlen = build_ptz_SetConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
            
    case eptzGetConfigurationOptions:
        rlen = build_ptz_GetConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case eptzGetPresetTours:
        rlen = build_ptz_GetPresetTours_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case eptzGetPresetTour:
        rlen = build_ptz_GetPresetTour_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzGetPresetTourOptions:
        rlen = build_ptz_GetPresetTourOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzCreatePresetTour:
        rlen = build_ptz_CreatePresetTour_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzModifyPresetTour:
        rlen = build_ptz_ModifyPresetTour_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzOperatePresetTour:
        rlen = build_ptz_OperatePresetTour_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eptzRemovePresetTour:
        rlen = build_ptz_RemovePresetTour_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case eptzSendAuxiliaryCommand:
        rlen = build_ptz_SendAuxiliaryCommand_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case eptzGeoMove:
        rlen = build_ptz_GeoMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    // onvif event service interfaces

    case etevGetServiceCapabilities:
        rlen = build_tev_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etevGetEventProperties:
        rlen = build_tev_GetEventProperties_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etevRenew:
        rlen = build_tev_Renew_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etevUnsubscribe:
        rlen = build_tev_Unsubscribe_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etevSubscribe:
        rlen = build_tev_Subscribe_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etevPauseSubscription:
        rlen = build_tev_PauseSubscription_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etevResumeSubscription:
        rlen = build_tev_ResumeSubscription_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etevCreatePullPointSubscription:
        rlen = build_tev_CreatePullPointSubscription_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etevDestroyPullPoint:
        rlen = build_tev_DestroyPullPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etevPullMessages:
        rlen = build_tev_PullMessages_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etevGetMessages:
        rlen = build_tev_GetMessages_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etevSeek:
        rlen = build_tev_Seek_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etevSetSynchronizationPoint:
        rlen = build_tev_SetSynchronizationPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    // onvif imaging service interfaces
    
    case eimgGetServiceCapabilities:
        rlen = build_img_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgGetImagingSettings:
        rlen = build_img_GetImagingSettings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case eimgSetImagingSettings:
        rlen = build_img_SetImagingSettings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;    

    case eimgGetOptions:
        rlen = build_img_GetOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
           
    case eimgMove:
        rlen = build_img_Move_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgStop:
        rlen = build_img_Stop_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgGetStatus:
        rlen = build_img_GetStatus_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgGetMoveOptions:
        rlen = build_img_GetMoveOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;        

    case eimgGetPresets:
        rlen = build_img_GetPresets_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgGetCurrentPreset:
        rlen = build_img_GetCurrentPreset_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case eimgSetCurrentPreset:
        rlen = build_img_SetCurrentPreset_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

#ifdef DEVICEIO_SUPPORT

    // onvif device IO service interfaces

    case etmdGetServiceCapabilities:
        rlen = build_tmd_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etmdGetRelayOutputs:
        rlen = build_tmd_GetRelayOutputs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetRelayOutputOptions:
        rlen = build_tmd_GetRelayOutputOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdSetRelayOutputSettings:
        rlen = build_tmd_SetRelayOutputSettings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdSetRelayOutputState:
        rlen = build_tmd_SetRelayOutputState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetDigitalInputs:
        rlen = build_tmd_GetDigitalInputs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetDigitalInputConfigurationOptions:
        rlen = build_tmd_GetDigitalInputConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdSetDigitalInputConfigurations:
        rlen = build_tmd_SetDigitalInputConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetSerialPorts:
        rlen = build_tmd_GetSerialPorts_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetSerialPortConfiguration:
        rlen = build_tmd_GetSerialPortConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdSetSerialPortConfiguration:
        rlen = build_tmd_SetSerialPortConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdGetSerialPortConfigurationOptions:
        rlen = build_tmd_GetSerialPortConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etmdSendReceiveSerialCommand:
        rlen = build_tmd_SendReceiveSerialCommand_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of DEVICEIO_SUPPORT

#ifdef PROFILE_G_SUPPORT

    // onvif recording service interfaces

    case etrcGetServiceCapabilities:
        rlen = build_trc_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcCreateRecording:
        rlen = build_trc_CreateRecording_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
          
    case etrcDeleteRecording:
        rlen = build_trc_DeleteRecording_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordings:
        rlen = build_trc_GetRecordings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcSetRecordingConfiguration:
        rlen = build_trc_SetRecordingConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordingConfiguration:
        rlen = build_trc_GetRecordingConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordingOptions:
        rlen = build_trc_GetRecordingOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcCreateTrack:
        rlen = build_trc_CreateTrack_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcDeleteTrack:
        rlen = build_trc_DeleteTrack_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetTrackConfiguration:
        rlen = build_trc_GetTrackConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcSetTrackConfiguration:
        rlen = build_trc_SetTrackConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcCreateRecordingJob:
        rlen = build_trc_CreateRecordingJob_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcDeleteRecordingJob:
        rlen = build_trc_DeleteRecordingJob_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordingJobs:
        rlen = build_trc_GetRecordingJobs_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcSetRecordingJobConfiguration:
        rlen = build_trc_SetRecordingJobConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordingJobConfiguration:
        rlen = build_trc_GetRecordingJobConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcSetRecordingJobMode:
        rlen = build_trc_SetRecordingJobMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetRecordingJobState:
        rlen = build_trc_GetRecordingJobState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrcExportRecordedData:
        rlen = build_trc_ExportRecordedData_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcStopExportRecordedData:
        rlen = build_trc_StopExportRecordedData_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrcGetExportRecordedDataState:
        rlen = build_trc_GetExportRecordedDataState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    // onvif replay service interfaces

    case etrpGetServiceCapabilities:
        rlen = build_trp_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrpGetReplayUri:
        rlen = build_trp_GetReplayUri_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrpGetReplayConfiguration:
        rlen = build_trp_GetReplayConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etrpSetReplayConfiguration:
        rlen = build_trp_SetReplayConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    // onvif search service interfaces

    case etseGetServiceCapabilities:
        rlen = build_tse_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetRecordingSummary:
        rlen = build_tse_GetRecordingSummary_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etseGetRecordingInformation:
        rlen = build_tse_GetRecordingInformation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetMediaAttributes:
        rlen = build_tse_GetMediaAttributes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseFindRecordings:
        rlen = build_tse_FindRecordings_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetRecordingSearchResults:
        rlen = build_tse_GetRecordingSearchResults_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseFindEvents:
        rlen = build_tse_FindEvents_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetEventSearchResults:
        rlen = build_tse_GetEventSearchResults_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etseFindMetadata:
        rlen = build_tse_FindMetadata_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetMetadataSearchResults:
        rlen = build_tse_GetMetadataSearchResults_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseFindPTZPosition:
        rlen = build_tse_FindPTZPosition_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetPTZPositionSearchResults:
        rlen = build_tse_GetPTZPositionSearchResults_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseGetSearchState:
        rlen = build_tse_GetSearchState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etseEndSearch:
        rlen = build_tse_EndSearch_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;        

#endif // end of PROFILE_G_SUPPORT

    // onvif analytics service interfaces

    case etanGetServiceCapabilities:
        rlen = build_tan_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanGetSupportedRules:
        rlen = build_tan_GetSupportedRules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanCreateRules:
        rlen = build_tan_CreateRules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanDeleteRules:
        rlen = build_tan_DeleteRules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanGetRules:
        rlen = build_tan_GetRules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanModifyRules:
        rlen = build_tan_ModifyRules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanCreateAnalyticsModules:
        rlen = build_tan_CreateAnalyticsModules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanDeleteAnalyticsModules:
        rlen = build_tan_DeleteAnalyticsModules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanGetAnalyticsModules:
        rlen = build_tan_GetAnalyticsModules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanModifyAnalyticsModules:
        rlen = build_tan_ModifyAnalyticsModules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;        

    case etanGetSupportedAnalyticsModules:
        rlen = build_tan_GetSupportedAnalyticsModules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanGetRuleOptions:
        rlen = build_tan_GetRuleOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etanGetAnalyticsModuleOptions:
        rlen = build_tan_GetAnalyticsModuleOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etanGetSupportedMetadata:
        rlen = build_tan_GetSupportedMetadata_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#ifdef PROFILE_C_SUPPORT

    // onvif access control interfaces

    case etacGetServiceCapabilities:
        rlen = build_tac_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacGetAccessPointInfoList:
        rlen = build_tac_GetAccessPointInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
          
    case etacGetAccessPointInfo:
        rlen = build_tac_GetAccessPointInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etacGetAccessPointList:
        rlen = build_tac_GetAccessPointList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacGetAccessPoints:
        rlen = build_tac_GetAccessPoints_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacCreateAccessPoint:
        rlen = build_tac_CreateAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacSetAccessPoint:
        rlen = build_tac_SetAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacModifyAccessPoint:
        rlen = build_tac_ModifyAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacDeleteAccessPoint:
        rlen = build_tac_DeleteAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etacGetAreaInfoList:
        rlen = build_tac_GetAreaInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etacGetAreaInfo:
        rlen = build_tac_GetAreaInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etacGetAreaList:
        rlen = build_tac_GetAreaList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacGetAreas:
        rlen = build_tac_GetAreas_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacCreateArea:
        rlen = build_tac_CreateArea_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacSetArea:
        rlen = build_tac_SetArea_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etacModifyArea:
        rlen = build_tac_ModifyArea_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etacDeleteArea:
        rlen = build_tac_DeleteArea_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etacGetAccessPointState:
        rlen = build_tac_GetAccessPointState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etacEnableAccessPoint:
        rlen = build_tac_EnableAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etacDisableAccessPoint:
        rlen = build_tac_DisableAccessPoint_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    // onvif door control service interfaces

    case etdcGetServiceCapabilities:
        rlen = build_tdc_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcGetDoorInfoList:
        rlen = build_tdc_GetDoorInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcGetDoorInfo:
        rlen = build_tdc_GetDoorInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcGetDoorState:
        rlen = build_tdc_GetDoorState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcAccessDoor:
        rlen = build_tdc_AccessDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcLockDoor:
        rlen = build_tdc_LockDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcUnlockDoor:
        rlen = build_tdc_UnlockDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcDoubleLockDoor:
        rlen = build_tdc_DoubleLockDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcBlockDoor:
        rlen = build_tdc_BlockDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcLockDownDoor:
        rlen = build_tdc_LockDownDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcLockDownReleaseDoor:
        rlen = build_tdc_LockDownReleaseDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcLockOpenDoor:
        rlen = build_tdc_LockOpenDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcLockOpenReleaseDoor:
        rlen = build_tdc_LockOpenReleaseDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etdcGetDoors:
        rlen = build_tdc_GetDoors_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcGetDoorList:
        rlen = build_tdc_GetDoorList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcCreateDoor:
        rlen = build_tdc_CreateDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcSetDoor:
        rlen = build_tdc_SetDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcModifyDoor:
        rlen = build_tdc_ModifyDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etdcDeleteDoor:
        rlen = build_tdc_DeleteDoor_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
#endif // end of PROFILE_C_SUPPORT
    
#ifdef THERMAL_SUPPORT

    // onvif thermal service interfaces

    case etthGetServiceCapabilities:
        rlen = build_tth_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthGetConfigurations:
        rlen = build_tth_GetConfigurations_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthGetConfiguration:  
        rlen = build_tth_GetConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthSetConfiguration:
        rlen = build_tth_SetConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthGetConfigurationOptions:
        rlen = build_tth_GetConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthGetRadiometryConfiguration:
        rlen = build_tth_GetRadiometryConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthSetRadiometryConfiguration:
        rlen = build_tth_SetRadiometryConfiguration_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etthGetRadiometryConfigurationOptions:
        rlen = build_tth_GetRadiometryConfigurationOptions_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of THERMAL_SUPPORT

#ifdef CREDENTIAL_SUPPORT

    // onvif credential service interfaces

    case etcrGetServiceCapabilities:
        rlen = build_tcr_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialInfo:
        rlen = build_tcr_GetCredentialInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialInfoList:
        rlen = build_tcr_GetCredentialInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentials:
        rlen = build_tcr_GetCredentials_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialList:
        rlen = build_tcr_GetCredentialList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrCreateCredential:
        rlen = build_tcr_CreateCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrModifyCredential:
        rlen = build_tcr_ModifyCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrDeleteCredential:
        rlen = build_tcr_DeleteCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialState:
        rlen = build_tcr_GetCredentialState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrEnableCredential:
        rlen = build_tcr_EnableCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrDisableCredential:
        rlen = build_tcr_DisableCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

    case etcrSetCredential:
        rlen = build_tcr_SetCredential_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrResetAntipassbackViolation:
        rlen = build_tcr_ResetAntipassbackViolation_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetSupportedFormatTypes:
        rlen = build_tcr_GetSupportedFormatTypes_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialIdentifiers:
        rlen = build_tcr_GetCredentialIdentifiers_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrSetCredentialIdentifier:
        rlen = build_tcr_SetCredentialIdentifier_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrDeleteCredentialIdentifier:
        rlen = build_tcr_DeleteCredentialIdentifier_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrGetCredentialAccessProfiles:
        rlen = build_tcr_GetCredentialAccessProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrSetCredentialAccessProfiles:
        rlen = build_tcr_SetCredentialAccessProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etcrDeleteCredentialAccessProfiles:
        rlen = build_tcr_DeleteCredentialAccessProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of CREDENTIAL_SUPPORT

#ifdef ACCESS_RULES

    // onvif access rule interfaces

    case etarGetServiceCapabilities:
        rlen = build_tar_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarGetAccessProfileInfo:
        rlen = build_tar_GetAccessProfileInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarGetAccessProfileInfoList:
        rlen = build_tar_GetAccessProfileInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarGetAccessProfiles:
        rlen = build_tar_GetAccessProfiles_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarGetAccessProfileList:
        rlen = build_tar_GetAccessProfileList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarCreateAccessProfile:
        rlen = build_tar_CreateAccessProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarModifyAccessProfile:
        rlen = build_tar_ModifyAccessProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etarDeleteAccessProfile:
        rlen = build_tar_DeleteAccessProfile_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of ACCESS_RULES

#ifdef SCHEDULE_SUPPORT

    // onvif schedule service interfaces

    case etscGetServiceCapabilities:
        rlen = build_tsc_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetScheduleInfo:
        rlen = build_tsc_GetScheduleInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetScheduleInfoList:
        rlen = build_tsc_GetScheduleInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetSchedules:
        rlen = build_tsc_GetSchedules_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetScheduleList:
        rlen = build_tsc_GetScheduleList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscCreateSchedule:
        rlen = build_tsc_CreateSchedule_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscModifySchedule:
        rlen = build_tsc_ModifySchedule_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscDeleteSchedule:
        rlen = build_tsc_DeleteSchedule_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetSpecialDayGroupInfo:
        rlen = build_tsc_GetSpecialDayGroupInfo_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetSpecialDayGroupInfoList:
        rlen = build_tsc_GetSpecialDayGroupInfoList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetSpecialDayGroups:
        rlen = build_tsc_GetSpecialDayGroups_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetSpecialDayGroupList:
        rlen = build_tsc_GetSpecialDayGroupList_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
    
    case etscCreateSpecialDayGroup:
        rlen = build_tsc_CreateSpecialDayGroup_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscModifySpecialDayGroup:
        rlen = build_tsc_ModifySpecialDayGroup_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscDeleteSpecialDayGroup:
        rlen = build_tsc_DeleteSpecialDayGroup_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etscGetScheduleState:
        rlen = build_tsc_GetScheduleState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of SCHEDULE_SUPPORT
    
#ifdef RECEIVER_SUPPORT

    // onvif receiving service interfaces

    case etrvGetServiceCapabilities:
        rlen = build_trv_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvGetReceivers:
        rlen = build_trv_GetReceivers_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvGetReceiver:
        rlen = build_trv_GetReceiver_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvCreateReceiver:
        rlen = build_trv_CreateReceiver_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvDeleteReceiver:
        rlen = build_trv_DeleteReceiver_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvConfigureReceiver:
        rlen = build_trv_ConfigureReceiver_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvSetReceiverMode:
        rlen = build_trv_SetReceiverMode_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etrvGetReceiverState:
        rlen = build_trv_GetReceiverState_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
#endif // end of RECEIVER_SUPPORT

#ifdef PROVISIONING_SUPPORT

    case etpvGetServiceCapabilities:
        rlen = build_tpv_GetServiceCapabilities_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvPanMove:
        rlen = build_tpv_PanMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvTiltMove:
        rlen = build_tpv_TiltMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvZoomMove:
        rlen = build_tpv_ZoomMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvRollMove:
        rlen = build_tpv_RollMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvFocusMove:
        rlen = build_tpv_FocusMove_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvStop:
        rlen = build_tpv_Stop_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;
        
    case etpvGetUsage:
        rlen = build_tpv_GetUsage_xml(p_buf+offset, mlen-offset, p_dev, p_req);
        break;

#endif // end of PROVISIONING_SUPPORT

    default:
        assert(FALSE);
        break;
    }

    offset += rlen;
    offset += snprintf(p_buf+offset, mlen-offset, "%s", soap_tailer);
    
    return offset;
}




