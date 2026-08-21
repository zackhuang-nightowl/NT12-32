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
#include "hxml.h"
#include "xml_node.h"
#include "onvif_probe.h"
#include "onvif.h"
#include "soap_parser.h"
#include "onvif_utils.h"
#include "util.h"

/***************************************************************************************/

#define ONVIF_GRP_ADDR      "239.255.255.250"
#define ONVIF_GRP_PORT      3702

/***************************************************************************************/

ONVIF_PROBE_CLS g_probe_cls = { NULL, NULL, sys_os_create_mutex(), 0, {0,0,0,0,0,0,0,0}, 
                                30, FALSE, {'\0'}, 0, NULL, NULL, sys_os_create_mutex() };

/***************************************************************************************/
SOCKET onvif_probe_init(uint32 ip)
{    
    int opt = 1;
    SOCKET fd;
    struct sockaddr_in addr;
    struct ip_mreq mcast;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, socket SOCK_DGRAM error!\r\n", __FUNCTION__);
        return 0;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ONVIF_GRP_PORT);
    addr.sin_addr.s_addr = ip;

    /* reuse socket addr */  
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt))) 
    {  
        log_print(HT_LOG_WARN, "%s, setsockopt SO_REUSEADDR error!\r\n", __FUNCTION__);
    }
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        log_print(HT_LOG_WARN, "%s, bind port %d failed\r\n", __FUNCTION__, ONVIF_GRP_PORT);
        
        // if port 3702 already occupied, only receive unicast message
        addr.sin_port = 0;
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
            closesocket(fd);
            log_print(HT_LOG_ERR, "%s, bind error! %s\r\n", __FUNCTION__, sys_os_get_socket_error());
            return 0;
        }
    }    
                      
    memset(&mcast, 0, sizeof(mcast));
    mcast.imr_multiaddr.s_addr = inet_addr(ONVIF_GRP_ADDR);
    mcast.imr_interface.s_addr = ip;

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mcast, sizeof(mcast)) < 0)
    {
#if __WINDOWS_OS__
        if (setsockopt(fd, IPPROTO_IP, 5, (char*)&mcast, sizeof(mcast)) < 0)
#endif        
        {
            closesocket(fd);
            log_print(HT_LOG_ERR, "%s, setsockopt IP_ADD_MEMBERSHIP error! %s\r\n", __FUNCTION__, sys_os_get_socket_error());
            return 0;
        }
    }

    return fd;
}

char probe_req1[] = 
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<Envelope xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
        "xmlns=\"http://www.w3.org/2003/05/soap-envelope\">"
    "<Header>"
    "<wsa:MessageID xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">uuid:%s</wsa:MessageID>"
    "<wsa:To xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
    "<wsa:Action xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
    "</Header>"
    "<Body>"
    "<Probe xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
        "xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
    "<Types>tds:Device</Types>"
    "<Scopes />"
    "</Probe>"
    "</Body>"
    "</Envelope>";    

char probe_req2[] = 
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<Envelope xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\" "
        "xmlns=\"http://www.w3.org/2003/05/soap-envelope\">"
    "<Header>"
    "<wsa:MessageID xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">uuid:%s</wsa:MessageID>"
    "<wsa:To xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
    "<wsa:Action xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
    "</Header>"
    "<Body>"
    "<Probe xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
        "xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
    "<Types>dn:NetworkVideoTransmitter</Types>"
    "<Scopes />"
    "</Probe>"
    "</Body>"
    "</Envelope>";

int onvif_probe_req_tx(SOCKET fd)
{
    int len;
    int rlen;
    char uuid[100] = {'\0'};
    char  * p_bufs = NULL;
    struct sockaddr_in addr;

    int buflen = 10*1024;
    
    p_bufs = (char *)malloc(buflen);
    if (NULL == p_bufs)
    {
        return -1;
    }
    
    memset(p_bufs, 0, buflen);
    snprintf(p_bufs, buflen, probe_req1, onvif_uuid_create(uuid, sizeof(uuid)));

    memset(&addr, 0, sizeof(addr));
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ONVIF_GRP_ADDR);
    addr.sin_port = htons(ONVIF_GRP_PORT);

    len = (int)strlen(p_bufs);
    rlen = sendto(fd, p_bufs, len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (rlen != len)
    {
        log_print(HT_LOG_ERR, "%s, rlen = %d, slen = %d\r\n", __FUNCTION__, rlen, len);
    }

    usleep(1000);
    
    memset(p_bufs, 0, buflen);
    snprintf(p_bufs, buflen, probe_req2, onvif_uuid_create(uuid, sizeof(uuid)));
        
    len = (int)strlen(p_bufs);
    rlen = sendto(fd, p_bufs, len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (rlen != len)
    {
        log_print(HT_LOG_ERR, "%s, rlen = %d, slen = %d\r\n", __FUNCTION__, rlen, len);
    }    

    free(p_bufs);

    return rlen;
}

BOOL onvif_parse_scopes(const char * scopes, DEVICE_BINFO * p_res)
{
    uint32 n = 0;
    char buff[128];
    const char * p = scopes;

    while (*p == ' ') p++;
    
    while (*p != '\0')
    {
        if (*p == ' ')
        {
            uint32 idx = p_res->sizeScopes;
            
            buff[n] = '\0';
            
            strcpy(p_res->scopes[idx].ScopeItem, buff);

            p_res->sizeScopes++;
            if (p_res->sizeScopes >= ARRAY_SIZE(p_res->scopes))
            {
                break;
            }

            p++;
            while (*p == ' ') p++;

            n = 0;
            continue;
        }
        else if (n < sizeof(buff)-1)
        {
            buff[n++] = *p;
        }

        p++;
    }

    if (n > 0)
    {
        buff[n] = '\0';    
        
        if (p_res->sizeScopes < ARRAY_SIZE(p_res->scopes))
        {            
            strcpy(p_res->scopes[p_res->sizeScopes].ScopeItem, buff);

            p_res->sizeScopes++;
        }
    }

    return TRUE;
}

BOOL onvif_parse_device_binfo(XMLN * p_node, DEVICE_BINFO * p_res)
{
    XMLN * p_EndpointReference;
    XMLN * p_Types;
    XMLN * p_XAddrs;
    XMLN * p_MetadataVersion;
    XMLN * p_Scopes;

    p_EndpointReference = xml_node_soap_get(p_node, "EndpointReference");
    if (p_EndpointReference)
    {
        XMLN * p_Address = xml_node_soap_get(p_EndpointReference, "Address");
        if (p_Address && p_Address->data)
        {
            const char * p = strrchr(p_Address->data, ':');
            if (p)
            {
                strncpy(p_res->EndpointReference, p+1, sizeof(p_res->EndpointReference)-1);
            }
            else
            {
                strncpy(p_res->EndpointReference, p_Address->data, sizeof(p_res->EndpointReference)-1);
            }
        }
    }
    
    p_Types = xml_node_soap_get(p_node, "Types");
    if (p_Types && p_Types->data)
    {
        p_res->type = parse_DeviceType(p_Types->data);
    }

    p_MetadataVersion = xml_node_soap_get(p_node, "MetadataVersion");
    if (p_MetadataVersion && p_MetadataVersion->data)
    {
        p_res->MetadataVersion = atoi(p_MetadataVersion->data);
    }

    p_Scopes = xml_node_soap_get(p_node, "Scopes");
    if (p_Scopes && p_Scopes->data)
    {
        onvif_parse_scopes(p_Scopes->data, p_res);
    }
    
    p_XAddrs = xml_node_soap_get(p_node, "XAddrs");
    if (p_XAddrs && p_XAddrs->data)
    {
        parse_XAddr(p_XAddrs->data, &p_res->XAddr);

        if (p_res->XAddr.host[0] == '\0' || p_res->XAddr.port == 0)
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

void onvif_probe_callback(DEVICE_BINFO * p_res, int msgtype)
{
    sys_os_mutex_enter(g_probe_cls.probe_mutex);
    if (g_probe_cls.probe_cb)
    {
        g_probe_cls.probe_cb(p_res, msgtype, g_probe_cls.probe_cb_data);
    }
    sys_os_mutex_leave(g_probe_cls.probe_mutex);
}

void onvif_monitor_cb(DEVICE_BINFO * p_res, int msgtype)
{
    sys_os_mutex_enter(g_probe_cls.monitor_mutex);

    if (strcmp(p_res->EndpointReference, g_probe_cls.monitor_reference) == 0 && 
        msgtype == g_probe_cls.monitor_msgtype)
    {
        if (g_probe_cls.monitor_cb)
        {
            g_probe_cls.monitor_cb(p_res, msgtype, g_probe_cls.monitor_cb_data);
        }
    }
    
    sys_os_mutex_leave(g_probe_cls.monitor_mutex);
}

BOOL onvif_probe_res(XMLN * p_node, DEVICE_BINFO * p_res)
{
    XMLN * p_body = xml_node_soap_get(p_node, "Body");
    if (NULL == p_body)
    {
        return FALSE;
    }
    
    XMLN * p_ProbeMatches = xml_node_soap_get(p_body, "ProbeMatches");
    if (p_ProbeMatches)
    {
        XMLN * p_ProbeMatch = xml_node_soap_get(p_ProbeMatches, "ProbeMatch");
        while (p_ProbeMatch && soap_strcmp(p_ProbeMatch->name, "ProbeMatch") == 0)
        {
            if (onvif_parse_device_binfo(p_ProbeMatch, p_res))
            {
                if (p_res->type != ODT_UNKNOWN)
                {
                    onvif_probe_callback(p_res, PROBE_MSGTYPE_MATCH);

                    onvif_monitor_cb(p_res, PROBE_MSGTYPE_MATCH);
                }
                else
                {
                    log_print(HT_LOG_ERR, "%s, unknow device type\r\n", __FUNCTION__);
                }
            }
            else
            {
                log_print(HT_LOG_ERR, "%s, parse device info failed\r\n", __FUNCTION__);
            }

            p_ProbeMatch = p_ProbeMatch->next;
        }
    }
    else
    {
        XMLN * p_Hello = xml_node_soap_get(p_body, "Hello");
        if (p_Hello)
        {    
            if (onvif_parse_device_binfo(p_Hello, p_res))
            {
                if (p_res->type != ODT_UNKNOWN)
                {
                    onvif_probe_callback(p_res, PROBE_MSGTYPE_HELLO);
                    
                    onvif_monitor_cb(p_res, PROBE_MSGTYPE_HELLO);
                }
                else
                {
                    log_print(HT_LOG_ERR, "%s, unknow device type\r\n", __FUNCTION__);
                }
            }
            else
            {
                log_print(HT_LOG_ERR, "%s, parse device info failed\r\n", __FUNCTION__);
            }
        }
        else
        {
            XMLN * p_Bye = xml_node_soap_get(p_body, "Bye");
            if (p_Bye)
            {
                XMLN * p_EndpointReference;

                p_EndpointReference = xml_node_soap_get(p_Bye, "EndpointReference");
                if (p_EndpointReference)
                {
                    XMLN * p_Address = xml_node_soap_get(p_EndpointReference, "Address");
                    if (p_Address && p_Address->data)
                    {
                        strncpy(p_res->EndpointReference, p_Address->data, sizeof(p_res->EndpointReference)-1);

                        onvif_probe_callback(p_res, PROBE_MSGTYPE_BYE);
                    
                        onvif_monitor_cb(p_res, PROBE_MSGTYPE_BYE);
                    }
                }
            }
        }
    }

    return TRUE;
}

int onvif_probe_net_rx()
{
    int i;
    int ret;
    int maxfd = 0;
    SOCKET fd = 0;
    char rbuf[8*1024];
    fd_set fdread;
    struct timeval tv = {1, 0};

    FD_ZERO(&fdread);

    for (i = 0; i < MAX_PROBE_FD; i++)
    {
        if (g_probe_cls.probe_fd[i] > 0)
        {
            FD_SET(g_probe_cls.probe_fd[i], &fdread); 

            if ((int)g_probe_cls.probe_fd[i] > maxfd)
            {
                maxfd = (int)g_probe_cls.probe_fd[i];
            }
        }
    }
    
    ret = select(maxfd+1, &fdread, NULL, NULL, &tv); 
    if (ret == 0) // Time expired 
    { 
        return 0; 
    }

    for (i = 0; i < MAX_PROBE_FD; i++)
    {
        if (g_probe_cls.probe_fd[i] > 0 && FD_ISSET(g_probe_cls.probe_fd[i], &fdread))
        {
            int rlen;
            XMLN * p_node;
            struct sockaddr_in addr;
            socklen_t addrlen;
            
            fd = g_probe_cls.probe_fd[i];
            
            addrlen = sizeof(struct sockaddr_in);
            rlen = recvfrom(fd, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&addr, &addrlen);
            if (rlen <= 0)
            {
                log_print(HT_LOG_ERR, "%s, rlen = %d, fd = %d\r\n", __FUNCTION__, rlen, fd);
                continue;
            }

            rbuf[rlen] = '\0';

            log_print(HT_LOG_DBG, "%s, len = %d, rbuf = %s\r\n", __FUNCTION__, rlen, rbuf);
            
            p_node = xxx_hxml_parse(rbuf, rlen);
            if (p_node == NULL)
            {
                log_print(HT_LOG_ERR, "%s, hxml parse err!!!\r\n", __FUNCTION__);
            }    
            else
            {
                DEVICE_BINFO res;
                memset(&res, 0, sizeof(DEVICE_BINFO));
                
                onvif_probe_res(p_node, &res);        
            }

            xml_node_del(p_node);
        }
    }

    return 1;
}

void onvif_probe_init_ifs()
{
    int ret;
    uint32 i, j;
    uint32 size = 15000;
    char * buff;
    HT_IFINFOTABLE * table;

    buff = (char *) malloc(size);
    if (NULL == buff)
    {
        return;
    }
    
    table = (HT_IFINFOTABLE *) buff;
    ret = get_ifinfo_table(table, &size);
    if (ret == 0)
    {
        free(buff);
        buff = (char *) malloc(size);
        if (NULL == buff)
        {
            return;
        }
        
        table = (HT_IFINFOTABLE *) buff;
        ret = get_ifinfo_table(table, &size);
    }

    if (ret != 1)
    {
        free(buff);
        return;
    }

    i = j = 0;
    
    for (; i < table->count && j < MAX_PROBE_FD; i++)
    {
        if (table->table[i].family != AF_INET)
        {
            continue;
        }
        
        uint32 ip = inet_addr(table->table[i].ip);        
        if (ip != 0 && ip != inet_addr("127.0.0.1"))
        {
            g_probe_cls.probe_fd[j++] = onvif_probe_init(ip);

            log_print(HT_LOG_INFO, "%s, probe init, ip=%s\r\n", __FUNCTION__, table->table[i].ip);
        }
    }

    free(buff);
}

void * onvif_probe_thread(void * argv)
{
    int i = 0;
    const char * sip = (const char *)argv;
    uint32 cur, prev = 0;

    if (NULL != sip && is_ipv4_address(sip))
    {
        g_probe_cls.probe_fd[i++] = onvif_probe_init(inet_addr(sip));

        log_print(HT_LOG_INFO, "%s, probe init, ip=%s\r\n", __FUNCTION__, sip);
    }
    else
    {
        onvif_probe_init_ifs();
    }
    
    while (g_probe_cls.probe_running)
    {
        cur = sys_os_get_uptime();
        
        if (cur - prev >= (uint32)g_probe_cls.probe_interval)
        {
            prev = cur;
            
            send_probe_req();
        }

        onvif_probe_net_rx();

        usleep(1000);
    }
    
    return NULL;
}

HT_API void set_monitor_cb(char * reference, int msgtype, onvif_probe_cb cb, void * pdata)
{
    sys_os_mutex_enter(g_probe_cls.monitor_mutex);

    if (reference)
    {
        strncpy(g_probe_cls.monitor_reference, reference, sizeof(g_probe_cls.monitor_reference)-1);
    }
    
    g_probe_cls.monitor_msgtype = msgtype;
    g_probe_cls.monitor_cb = cb;
    g_probe_cls.monitor_cb_data = pdata;
    
    sys_os_mutex_leave(g_probe_cls.monitor_mutex);
}

HT_API void set_probe_cb(onvif_probe_cb cb, void * pdata)
{
    sys_os_mutex_enter(g_probe_cls.probe_mutex);
    g_probe_cls.probe_cb = cb;
    g_probe_cls.probe_cb_data = pdata;
    sys_os_mutex_leave(g_probe_cls.probe_mutex);
}

HT_API void send_probe_req()
{
    int i;    
    for (i = 0; i < MAX_PROBE_FD; i++)
    {
        if (g_probe_cls.probe_fd[i] > 0)
        {
            onvif_probe_req_tx(g_probe_cls.probe_fd[i]);    
        }
    }
}

HT_API void set_probe_interval(int interval)
{
    g_probe_cls.probe_interval = interval;
    
    if (g_probe_cls.probe_interval <= 0)
    {
        g_probe_cls.probe_interval = 30;
    }
}

HT_API int start_probe(const char * ip, int interval)
{
    set_probe_interval(interval);

    g_probe_cls.probe_running = TRUE;
    g_probe_cls.probe_thread = sys_os_create_thread((void *)onvif_probe_thread, (void *)ip);

    return g_probe_cls.probe_thread ? 0 : -1;
}

HT_API void stop_probe()
{
    int i;
    
    g_probe_cls.probe_running = FALSE;

    sys_os_wait_thread(&g_probe_cls.probe_thread);

    for (i = 0; i < MAX_PROBE_FD; i++)
    {
        if (g_probe_cls.probe_fd[i] > 0)
        {
            closesocket(g_probe_cls.probe_fd[i]);
            g_probe_cls.probe_fd[i] = 0;
        }
    }
}



