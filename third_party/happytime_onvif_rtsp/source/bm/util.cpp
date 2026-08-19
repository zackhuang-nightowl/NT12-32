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
#include "util.h"

/***************************************************************************************/

HT_API BOOL is_ipv4_address(const char * address)
{
    struct in_addr ipv4;

    // Check if it is a valid IPv4 address
    int ret = inet_pton(AF_INET, address, &ipv4);
    if (ret == 1) 
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_ipv6_address(const char * address)
{
    struct in6_addr ipv6;

    // Check if it is a valid IPv6 address
    int ret = inet_pton(AF_INET6, address, &ipv6);
    if (ret == 1) 
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_ip_address(const char * address)
{
    return is_ipv4_address(address) || is_ipv6_address(address);
}

HT_API BOOL is_ipv4_local_address(struct in_addr * addr)
{
    uint8 * bytes = (uint8 *) addr;

    // Check for loopback address
    if (bytes[0] == 127) 
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_ipv6_local_address(struct in6_addr * addr)
{
    uint16 u16;
    struct in6_addr zero;

    memset(&zero, 0, sizeof(zero));
    
    u16 = addr->s6_addr[14];
    u16 = u16 << 8 | addr->s6_addr[15];
    
    // Check for loopback address (::1)
    if (memcmp(addr, &zero, 14) == 0 && u16 == 0x0001)
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_local_address(struct sockaddr * addr)
{
    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in * ipv4 = (struct sockaddr_in *) addr;
        return is_ipv4_local_address(&ipv4->sin_addr);
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * ipv6 = (struct sockaddr_in6 *) addr;
        return is_ipv6_local_address(&ipv6->sin6_addr);
    }

    return FALSE;
}

HT_API BOOL is_ipv4_linklocal_address(struct in_addr * addr)
{
    uint8 * bytes = (uint8 *) addr;

    // Link local addresss start with 169.254.0.0/16
    
    if (bytes[0] == 169 && bytes[1] == 254)
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_ipv6_linklocal_address(struct in6_addr * addr)
{
    uint8 * bytes = (uint8 *) addr;
    
    // Link-local addresses start with fe80::/10
    
    if ((bytes[0] == 0xFE) && ((bytes[1] & 0xC0) == 0x80))
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_linklocal_address(struct sockaddr * addr)
{
    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in * ipv4 = (struct sockaddr_in *) addr;
        return is_ipv4_linklocal_address(&ipv4->sin_addr);
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * ipv6 = (struct sockaddr_in6 *) addr;
        return is_ipv6_linklocal_address(&ipv6->sin6_addr);
    }

    return FALSE;
}

HT_API BOOL is_same_ipv4_network(struct in_addr * src, struct in_addr * dst, int prefix_len)
{
    uint32 mask = ~((1 << (32 - prefix_len)) - 1);
    
    if ((ntohl(src->s_addr) & mask) == (ntohl(dst->s_addr) & mask))
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_same_ipv6_network(struct in6_addr * src, struct in6_addr * dst, int prefix_len)
{
    struct in6_addr src_network_addr;
    struct in6_addr dst_network_addr;

    memset(&src_network_addr, 0, sizeof(struct in6_addr));
    memset(&dst_network_addr, 0, sizeof(struct in6_addr));
                
    memcpy(&src_network_addr, src, prefix_len / 8);
    if (prefix_len % 8) 
    {
        uint8 mask = 0xff << (8 - (prefix_len % 8));
        src_network_addr.s6_addr[prefix_len / 8] &= mask;
    }

    memcpy(&dst_network_addr, dst, prefix_len / 8);
    if (prefix_len % 8) 
    {
        uint8 mask = 0xff << (8 - (prefix_len % 8));
        dst_network_addr.s6_addr[prefix_len / 8] &= mask;
    }

    if (memcmp(&src_network_addr, &dst_network_addr, sizeof(struct in6_addr)) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

HT_API BOOL is_local_ip_net(const char * ip)
{
    int ret;
    BOOL flag = FALSE;
    uint32 i;
    uint32 size = 15000;
    char * buff;
    HT_IFINFOTABLE * table;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr * addr;
    
    if (is_ipv4_address(ip))
    {
        addr4.sin_family = AF_INET;
        inet_pton(AF_INET, ip, &addr4.sin_addr);

        addr = (struct sockaddr *) &addr4;

        if (addr4.sin_addr.s_addr == htonl(INADDR_LOOPBACK))
        {
            return TRUE;
        }
    }
    else if (is_ipv6_address(ip))
    {
        addr6.sin6_family = AF_INET6;
        inet_pton(AF_INET6, ip, &addr6.sin6_addr);

        addr = (struct sockaddr *) &addr6;

        if (memcmp(&addr6.sin6_addr, &in6addr_loopback, sizeof(struct in6_addr)) == 0)
        {
            return TRUE;
        }
    }
    else
    {
        return FALSE;
    }
    
    buff = (char *) malloc(size);
    if (NULL == buff)
    {
        return FALSE;
    }
    
    table = (HT_IFINFOTABLE *) buff;
    ret = get_ifinfo_table(table, &size);
    if (ret == 0)
    {
        free(buff);
        buff = (char *) malloc(size);
        if (NULL == buff)
        {
            return FALSE;
        }
        
        table = (HT_IFINFOTABLE *) buff;
        ret = get_ifinfo_table(table, &size);
    }

    if (ret != 1)
    {
        free(buff);
        return FALSE;
    }
    
    for (i = 0; i < table->count; i++)
    {
        if (table->table[i].family != addr->sa_family)
        {
            continue;
        }

        if (table->table[i].family == AF_INET)
        {
            struct in_addr saddr;
            inet_pton(AF_INET, table->table[i].ip, &saddr);
            
            if (is_same_ipv4_network(&addr4.sin_addr, &saddr, table->table[i].prefix_len))
            {
                flag = TRUE;
                break;
            }
        }
        else if (table->table[i].family == AF_INET6)
        {
            struct in6_addr saddr;
            inet_pton(AF_INET6, table->table[i].ip, &saddr);
            
            if (is_same_ipv6_network(&addr6.sin6_addr, &saddr, table->table[i].prefix_len))
            {
                flag = TRUE;
                break;
            }
        }
    }

    free(buff);

    return flag;
}

#if defined(ANDROID)

/**
 * The Android platform does not support the getifaddrs and freeifaddrs interfaces.
 * Using ioctl may only obtain IPV4 addresses.
 **/

HT_API void freeifaddrs(struct ifaddrs *ifap)
{
    struct ifaddrs *ifa;
    
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_name)
        {
            free(ifa->ifa_name);
        }

        if (ifa->ifa_addr)
        {
            free(ifa->ifa_addr);
        }

        if (ifa->ifa_netmask)
        {
            free(ifa->ifa_netmask);
        }
        
        free(ifa);
    }
}

HT_API int getifaddrs(struct ifaddrs **ifap)
{
    int fd, i, num, size;
    char buff[8190];
    struct ifreq *ifr;
    struct ifconf conf;
    struct ifaddrs *ifa_list = NULL, *ifa_last = NULL;
    
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd <= 0)
    {
        return -1;
    }
    
    conf.ifc_len = sizeof(buff);
    conf.ifc_buf = buff;
    
    if (ioctl(fd, SIOCGIFCONF, &conf) == -1)
    {
        close(fd);
        return -1;
    }
    
    num = conf.ifc_len / sizeof(struct ifreq);
    ifr = conf.ifc_req;
    
    for (i=0; i<num; i++)
    {
        if (ifr->ifr_addr.sa_family != AF_INET && ifr->ifr_addr.sa_family != AF_INET6)
        {
            ifr++;
            continue;
        }
        
        struct ifaddrs * ifa = (struct ifaddrs *) malloc(sizeof(struct ifaddrs));
        if (NULL == ifa)
        {
            close(fd);
            freeifaddrs(ifa_list);
            return -1;
        }

        memset(ifa, 0, sizeof(struct ifaddrs));

        if (ifa_last) 
        {
            ifa_last->ifa_next = ifa;
        } 
        else 
        {
            ifa_list = ifa;
        }
        
        ifa_last = ifa;
        
        ifa->ifa_name = strdup(ifr->ifr_name);

        if (ifr->ifr_addr.sa_family == AF_INET)
        {
            size = sizeof(struct sockaddr_in);
        }
        else if (ifr->ifr_addr.sa_family == AF_INET6)
        {
            size = sizeof(struct sockaddr_in6);
        }

        ifa->ifa_addr = (struct sockaddr *) malloc(size);
        if (NULL == ifa->ifa_addr)
        {
            close(fd);
            freeifaddrs(ifa_list);
            return -1;
        }

        memcpy(ifa->ifa_addr, &ifr->ifr_addr, size);
        
        ioctl(fd, SIOCGIFFLAGS, ifr);
        
        ifa->ifa_flags = ifr->ifr_flags;

        ioctl(fd, SIOCGIFNETMASK, ifr);
        
        ifa->ifa_netmask = (struct sockaddr *) malloc(size);
        if (NULL == ifa->ifa_netmask)
        {
            close(fd);
            freeifaddrs(ifa_list);
            return -1;
        }

        memcpy(ifa->ifa_netmask, &ifr->ifr_netmask, size);
        
        ifr++;
    }

    close(fd);
    
    *ifap = ifa_list;
    
    return 0;
}

#endif

HT_API int get_ifinfo_table(HT_IFINFOTABLE * table, uint32 * size)
{
#if __WINDOWS_OS__

    int ret = 0;
    int nums = 0;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;
    DWORD retVal;
    uint32 bufsize = *size;

    *size = sizeof(uint32);
    
    adapterAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufLen);
    if (NULL == adapterAddresses)
    {
        return -1;
    }

    retVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapterAddresses, &bufLen);
    if (retVal == ERROR_BUFFER_OVERFLOW) 
    {
        free(adapterAddresses);
        
        adapterAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufLen);
        if (NULL == adapterAddresses)
        {
            return -1;
        }

        retVal = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapterAddresses, &bufLen);
    }

    if (retVal == NO_ERROR)
    {
        PIP_ADAPTER_ADDRESSES adapter;

        for (adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next)
        {
            PIP_ADAPTER_UNICAST_ADDRESS unicast;

            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_PROP_VIRTUAL)
            {
                continue;
            }

            for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next)
            {
                if (unicast->Address.lpSockaddr->sa_family != AF_INET && unicast->Address.lpSockaddr->sa_family != AF_INET6)
                {
                    continue;
                }
                
                *size += sizeof(HT_IFINFO);

                if (bufsize < *size)
                {
                    continue;
                }

                table->table[nums].family = unicast->Address.lpSockaddr->sa_family;
                table->table[nums].prefix_len = unicast->OnLinkPrefixLength;
                
                if (unicast->Address.lpSockaddr->sa_family == AF_INET) 
                {
                    struct sockaddr_in * addr = (struct sockaddr_in *)unicast->Address.lpSockaddr;
                    
                    // IPv4 address
                    inet_ntop(AF_INET, &addr->sin_addr, table->table[nums].ip, sizeof(table->table[nums].ip));
                }
                else if (unicast->Address.lpSockaddr->sa_family == AF_INET6)
                {
                    struct sockaddr_in6 * addr = (struct sockaddr_in6 *)unicast->Address.lpSockaddr;
                    
                    // IPv6 address
                    inet_ntop(AF_INET6, &addr->sin6_addr, table->table[nums].ip, sizeof(table->table[nums].ip));
                }
                
                nums++;
            }
        }
    }
    else 
    {
        log_print(HT_LOG_WARN, "%s, GetAdaptersAddresses error: %lu\n", __FUNCTION__, retVal);
    }

    if (adapterAddresses != NULL) 
    {
        free(adapterAddresses);
    }

    if (bufsize >= sizeof(uint32))
    {
        table->count = nums;
    }
    
    if (bufsize < *size)
    {
        return 0;
    }
    else
    {
        return 1;
    }

#elif __LINUX_OS__

    int nums = 0;
    uint32 bufsize = *size;
    struct ifaddrs *if_addr;
    struct ifaddrs *if_addrs = NULL;

    *size = sizeof(uint32);
    
    if (getifaddrs(&if_addrs) != 0)
    {
        log_print(HT_LOG_ERR, "%s, getifaddrs failed\r\n", __FUNCTION__);
        return -1;
    }
    
    for (if_addr = if_addrs; if_addr != NULL; if_addr = if_addr->ifa_next)
    {
        if (NULL == if_addr->ifa_addr)
        {
            continue;
        }
        
        if (!(if_addr->ifa_flags & IFF_UP))
        {
            continue;
        }

        if (if_addr->ifa_addr->sa_family != AF_INET && if_addr->ifa_addr->sa_family != AF_INET6)
        {
            continue;
        }

        *size += sizeof(HT_IFINFO);

        if (bufsize < *size)
        {
            continue;
        }

        table->table[nums].family = if_addr->ifa_addr->sa_family;
        table->table[nums].prefix_len = get_mask_length_by_sockaddr(if_addr->ifa_netmask);
        
        if (if_addr->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in * addr = (struct sockaddr_in *) if_addr->ifa_addr;
            
            // IPv4 address
            inet_ntop(AF_INET, &addr->sin_addr, table->table[nums].ip, sizeof(table->table[nums].ip));
        }
        else if (if_addr->ifa_addr->sa_family == AF_INET6)
        {
            struct sockaddr_in6 * addr = (struct sockaddr_in6 *) if_addr->ifa_addr;
            
            // IPv6 address
            inet_ntop(AF_INET6, &addr->sin6_addr, table->table[nums].ip, sizeof(table->table[nums].ip));
        }

        nums++;
    }
    
    freeifaddrs(if_addrs);

    if (bufsize >= sizeof(uint32))
    {
        table->count = nums;
    }
    
    if (bufsize < *size)
    {
        return 0;
    }
    else
    {
        return 1;
    }
    
#endif

    return -1;
}

HT_API BOOL get_route_if_ip(struct sockaddr * dst_ip, struct sockaddr * if_ip)
{
    BOOL ret = FALSE;

#if __WINDOWS_OS__
    
    DWORD idx = -1, retVal;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;

    retVal = GetBestInterfaceEx(dst_ip, &idx);

    adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
    if (NULL == adapterAddresses)
    {
        return FALSE;
    }

    retVal = GetAdaptersAddresses(dst_ip->sa_family, flags, NULL, adapterAddresses, &bufLen);
    if (retVal == ERROR_BUFFER_OVERFLOW)
    {
        free(adapterAddresses);

        adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
        if (NULL == adapterAddresses)
        {
            return FALSE;
        }

        retVal = GetAdaptersAddresses(dst_ip->sa_family, flags, NULL, adapterAddresses, &bufLen);
    }

    if (retVal == NO_ERROR)
    {
        PIP_ADAPTER_ADDRESSES adapter;

        for (adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next)
        {
            PIP_ADAPTER_UNICAST_ADDRESS unicast;

            if (idx != -1)
            {
                if (adapter->IfIndex == idx)
                {
                    for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next)
                    {
                        if (unicast->Address.lpSockaddr->sa_family == AF_INET)
                        {
                            memcpy(if_ip, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in));
                        }
                        else
                        {
                            memcpy(if_ip, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in6));
                        }

                        ret = TRUE;
                        break;
                    }

                    break;
                }
            }
            else
            {
                for (unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next)
                {
                    if (is_local_address(unicast->Address.lpSockaddr))
                    {
                        continue;
                    }

                    if (is_linklocal_address(unicast->Address.lpSockaddr))
                    {
                        continue;
                    }

                    if (unicast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        memcpy(if_ip, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in));
                    }
                    else
                    {
                        memcpy(if_ip, unicast->Address.lpSockaddr, sizeof(struct sockaddr_in6));
                    }

                    ret = TRUE;
                    break;
                }

                if (ret)
                {
                    break;
                }
            }
        }
    }
    else
    {
        log_print(HT_LOG_WARN, "%s, GetAdaptersAddresses error: %lu\n", __FUNCTION__, retVal);
    }

    if (adapterAddresses != NULL)
    {
        free(adapterAddresses);
    }

    return ret;
    
#elif __LINUX_OS__

    struct ifaddrs *if_addr;
    struct ifaddrs *if_addrs = NULL;
    
    if (0 != getifaddrs(&if_addrs))
    {
        log_print(HT_LOG_ERR, "%s, getifaddrs failed\r\n", __FUNCTION__);
        return FALSE;
    }

    for (if_addr = if_addrs; if_addr != NULL; if_addr = if_addr->ifa_next)
    {
        if (NULL == if_addr->ifa_addr)
        {
            continue;
        }

        if ((if_addr->ifa_flags & IFF_LOOPBACK) || !(if_addr->ifa_flags & IFF_UP))
        {
            continue;
        }

        if (if_addr->ifa_addr->sa_family != dst_ip->sa_family)
        {
            continue;
        }

        if (is_local_address(if_addr->ifa_addr))
        {
            continue;
        }

        if (is_linklocal_address(if_addr->ifa_addr))
        {
            continue;
        }
            
        if (if_addr->ifa_addr->sa_family == AF_INET)
        {
            memcpy(if_ip, if_addr->ifa_addr, sizeof(struct sockaddr_in));
        }
        else if (if_addr->ifa_addr->sa_family == AF_INET6)
        {
            memcpy(if_ip, if_addr->ifa_addr, sizeof(struct sockaddr_in6));
        }

        ret = TRUE;
        break;
    }
    
    freeifaddrs(if_addrs);
    
    return ret;
    
#endif

    return FALSE;
}

HT_API BOOL get_default_if_ip(struct sockaddr * addr)
{
    BOOL ret = FALSE;
    struct sockaddr_in addr_ip4;
    struct sockaddr_in6 addr_ip6;

    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));

        dest.sin_family = AF_INET;

        ret = get_route_if_ip((struct sockaddr*)&dest, (struct sockaddr*)&addr_ip4);
        if (ret)
        {
            memcpy(addr, &addr_ip4, sizeof(struct sockaddr_in));
        }
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 dest;
        memset(&dest, 0, sizeof(dest));

        dest.sin6_family = AF_INET6;

        ret = get_route_if_ip((struct sockaddr*)&dest, (struct sockaddr*)&addr_ip6);
        if (ret)
        {
            memcpy(addr, &addr_ip6, sizeof(struct sockaddr_in6));
        }
    }
    else
    {
        return FALSE;
    }

    if (ret)
    {
        return TRUE;
    }
    else
    {
        int r;
        char* buff = NULL;
        uint32 size = 15000;
        HT_IFINFOTABLE* table = (HT_IFINFOTABLE*)buff;

        buff = (char*)malloc(size);
        if (NULL == buff)
        {
            return FALSE;
        }

        table = (HT_IFINFOTABLE*)buff;

        r = get_ifinfo_table(table, &size);
        if (r == 0)
        {
            free(buff);
            buff = (char*)malloc(size);
            if (NULL == buff)
            {
                return FALSE;
            }

            table = (HT_IFINFOTABLE*)buff;

            r = get_ifinfo_table(table, &size);
        }

        if (r > 0)
        {
            uint32 i;

            for (i = 0; i < table->count; i++)
            {
                if (table->table[i].family == addr->sa_family)
                {
                    if (addr->sa_family == AF_INET)
                    {
                        struct sockaddr_in * addr4 = (struct sockaddr_in *) addr;
                        
                        inet_pton(addr->sa_family, table->table[i].ip, &addr4->sin_addr);
                    }
                    else if (addr->sa_family == AF_INET6)
                    {
                        struct sockaddr_in6 * addr6 = (struct sockaddr_in6 *) addr;
                        
                        inet_pton(addr->sa_family, table->table[i].ip, &addr6->sin6_addr);
                    }

                    ret = TRUE;
                    break;
                }
            }
        }
        else
        {
            ret = FALSE;
        }

        free(buff);
        return ret;
    }
}

HT_API char * get_local_ip(uint16 family, char * ip, int size)
{
    BOOL ret = FALSE;
    struct sockaddr_in addr_ip4;
    struct sockaddr_in6 addr_ip6;

    if (family == AF_INET)
    {
        addr_ip4.sin_family = AF_INET;

        ret = get_default_if_ip((struct sockaddr*)&addr_ip4);
        if (ret)
        {
            inet_ntop(AF_INET, &addr_ip4.sin_addr, ip, size);
        }
    }
    else if (family == AF_INET6)
    {
        addr_ip6.sin6_family = AF_INET6;

        ret = get_default_if_ip((struct sockaddr*)&addr_ip6);
        if (ret)
        {
            inet_ntop(AF_INET6, &addr_ip6.sin6_addr, ip, size);
        }
    }
    
    if (ret)
    {
        return ip;
    }

    if (family == AF_INET)
    {
        strncpy(ip, "127.0.0.1", size);
    }
    else if (family == AF_INET6)
    {
        strncpy(ip, "::1", size);
    }

    return ip;
}

HT_API const char * get_default_gateway()
{   
    static char gateway[64] = {'\0'};
    
#if __WINDOWS_OS__

    IP_ADAPTER_INFO AdapterInfo[16];            // Allocate information for up to 16 NICs  
    DWORD dwBufLen = sizeof(AdapterInfo);       // Save the memory size of buffer  
  
    DWORD dwStatus = GetAdaptersInfo(           // Call GetAdapterInfo  
        AdapterInfo,                            // [out] buffer to receive data  
        &dwBufLen);                             // [in] size of receive data buffer  
  
    PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;// Contains pointer to current adapter info  
    if (NULL == pAdapterInfo)
    {        
        return NULL;
    } 

    memset(gateway, 0, sizeof(gateway));

    while (pAdapterInfo)
    {
        if (strcmp(pAdapterInfo->GatewayList.IpAddress.String, "0.0.0.0"))
        {
            strncpy(gateway, pAdapterInfo->GatewayList.IpAddress.String, sizeof(gateway)-1);
            break;
        }
        
        pAdapterInfo = pAdapterInfo->Next;
    }

#elif __LINUX_OS__
    
    char line[100], *p, *c, *g, *saveptr;
    int ret = 0;
    
    FILE *fp = fopen("/proc/net/route" , "r");
    if (NULL == fp)
    {
        return NULL;
    }

    memset(gateway, 0, sizeof(gateway));
    
    while (fgets(line, 100, fp))
    {
        p = strtok_r(line, " \t", &saveptr);
        c = strtok_r(NULL, " \t", &saveptr);
        g = strtok_r(NULL, " \t", &saveptr);

        if (p != NULL && c != NULL)
        {
            if (strcmp(c, "00000000") == 0)
            {
                if (g)
                {
                    char *p_end;
                    int gw = strtol(g, &p_end, 16);
                    
                    struct in_addr addr;
                    addr.s_addr = gw;
                    
                    strcpy(gateway, inet_ntoa(addr));
                    ret = 1;
                }
                
                break;
            }
        }
    }

    fclose(fp);
    
    if (ret == 0)
    {
        return NULL;
    }

#endif

    return gateway;
}

HT_API const char * get_dns_server()
{
    static char dns[64] = {'\0'};

#if __WINDOWS_OS__

    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES adapterAddresses = NULL;
    DWORD retVal;
    
    adapterAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufLen);
    if (NULL == adapterAddresses)
    {
        return dns;
    }

    retVal = GetAdaptersAddresses(AF_INET, flags, NULL, adapterAddresses, &bufLen);
    if (retVal == ERROR_BUFFER_OVERFLOW) 
    {
        free(adapterAddresses);
        
        adapterAddresses = (IP_ADAPTER_ADDRESSES *) malloc(bufLen);
        if (NULL == adapterAddresses)
        {
            return dns;
        }

        retVal = GetAdaptersAddresses(AF_INET, flags, NULL, adapterAddresses, &bufLen);
    }

    if (retVal == NO_ERROR)
    {
        PIP_ADAPTER_ADDRESSES adapter;

        for (adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next)
        {
            PIP_ADAPTER_DNS_SERVER_ADDRESS dnsaddr;

            if (adapter->OperStatus != IfOperStatusUp || 
                adapter->IfType == IF_TYPE_PROP_VIRTUAL || 
                adapter->IfType & IF_TYPE_SOFTWARE_LOOPBACK)
            {
                continue;
            }

            dnsaddr = adapter->FirstDnsServerAddress;
            if (dnsaddr)
            {
                get_sockaddr_ip(dnsaddr->Address.lpSockaddr, dns, sizeof(dns));
                break;
            }
        }
    }

    if (adapterAddresses != NULL) 
    {
        free(adapterAddresses);
    }

#elif __LINUX_OS__

    uint32 i;
    char line[100] = {'\0'};
    char * p;
    FILE * fp;
    
    memset(dns, 0, sizeof(dns));

    fp = fopen("/etc/resolv.conf" , "r");
    if (NULL == fp)
    {
        log_print(HT_LOG_ERR, "open file /etc/resolv.conf failed\r\n");
        return dns;
    }
    
    while (fgets(line, 100, fp))
    {
        p = line;
        
        while (*p == ' ') p++;  // skip space
        
        if (strncasecmp(p, "nameserver", 10) != 0)
        {
            continue;
        }
        
        p += 10;
        
        while (*p == ' ') p++;  // skip space
        
        i = 0;
        
        while (*p != '\0')
        {
            if (*p == ' ' || *p == '\n')
            {
                break;
            }

            if (i < sizeof(dns) - 1)
            {
                dns[i++] = *p++;
            }
            else
            {
                break;
            }
        }
        
        dns[i] = '\0';
        
        if (is_ip_address(dns))
        {
            break;
        }
        else
        {
            dns[0] = '\0';
        }
    }
#endif

    return dns;
}

HT_API int get_mask_length_by_sockaddr(struct sockaddr * mask)
{
    int masklen = 0;

    if (mask->sa_family == AF_INET)
    {
        const struct sockaddr_in * addr = (const struct sockaddr_in *) mask;
        uint32 m = ntohl(addr->sin_addr.s_addr);

        while (m & 0x80000000) 
        {
            masklen++;
            m <<= 1;
        }
    }
    else if (mask->sa_family == AF_INET6)
    {
        int i;
        const struct sockaddr_in6 *addr = (const struct sockaddr_in6 *) mask;
        const uint8 * m = addr->sin6_addr.s6_addr;

        for (i = 0; i < 16; ++i) 
        {
            if (m[i] == 0xff) 
            {
                masklen += 8;
            }
            else 
            {
                // Process the remaining bits in the first non-0xff byte
                uint8 byte = m[i];
                while (byte & 0x80) 
                {
                    masklen++;
                    byte <<= 1;
                }
                // Exit loop as we've found the first non-contiguous bit
                break;
            }
        }
    }

    return masklen;
}

HT_API int get_mask_length(const char * mask)
{
    int masklen = 0;
    
    if (is_ipv4_address(mask))
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, mask, &addr.sin_addr);
        masklen = get_mask_length_by_sockaddr((struct sockaddr *)&addr);
    }
    else if (is_ipv6_address(mask))
    {
        struct sockaddr_in6 addr;
        addr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, mask, &addr.sin6_addr);
        masklen = get_mask_length_by_sockaddr((struct sockaddr *)&addr);
    }

    return masklen;
}

HT_API const char * get_mask_by_length(int family, int masklen, char * mask, int size)
{
    if (family == AF_INET)
    {
        struct in_addr addr;
        uint32 ip = 0xFFFFFFFF << (32 - masklen);
        
        addr.s_addr = htonl(ip);
        
        inet_ntop(family, &addr, mask, size);
    }
    else if (family == AF_INET6)
    {
        int i;
        struct in6_addr addr;
        memset(&addr, 0, sizeof(struct in6_addr));

        for (i = 0; i < 16 && masklen > 0; ++i, masklen -= 8)
        {
            uint8 byte = (uint8)(0xFF << (8 - ((masklen < 8) ? masklen : 8)));
            ((uint8 *)&addr)[i] = byte;
        }
        
        inet_ntop(family, &addr, mask, size);
    }

    return mask;
}

HT_API int get_sockaddr_port(struct sockaddr * addr)
{
    int port = 0;
    
    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in * addr_in = (struct sockaddr_in*) addr;
        
        port = ntohs(addr_in->sin_port);
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * addr_in = (struct sockaddr_in6*) addr;
        
        port = ntohs(addr_in->sin6_port);
    }

    return port;
}

HT_API const char * get_sockaddr_ip(struct sockaddr * addr, char * str, int str_len)
{
    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in * addr_in = (struct sockaddr_in*) addr;
        
        inet_ntop(addr->sa_family, &addr_in->sin_addr, str, str_len);
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * addr_in = (struct sockaddr_in6*) addr;
        
        inet_ntop(addr->sa_family, &addr_in->sin6_addr, str, str_len);
    }

    return str;
}

HT_API BOOL get_address_by_name(const char * host_name, HT_PROTOCOL protocol, HT_SOCKADDR * addr)
{
    int ret;
    struct addrinfo hints;
    struct addrinfo* ptr = NULL;
    struct addrinfo* result = NULL;

    memset(addr, 0, sizeof(HT_SOCKADDR));
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;

    if (protocol == HT_PROTOCOL_UDP)
    {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
    }
    else if (protocol == HT_PROTOCOL_TCP)
    {
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
    }
    else
    {
        return FALSE;
    }
    
    ret = getaddrinfo(host_name, NULL, &hints, &result);
    if (ret != 0)
    {
        return FALSE;
    }

    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        if (ptr->ai_family == AF_INET)
        {
            addr->ipv4_flag = 1;
            memcpy(&addr->ipv4_addr, ptr->ai_addr, sizeof(struct sockaddr_in));
        }
        else if (ptr->ai_family == AF_INET6)
        {
            addr->ipv6_flag = 1;
            memcpy(&addr->ipv6_addr, ptr->ai_addr, sizeof(struct sockaddr_in6));
        }
    }

    freeaddrinfo(result);

    return TRUE;
}

HT_API char * lowercase(char * str) 
{
    uint32 i;
    
    for (i = 0; i < strlen(str); ++i)
    {
        str[i] = tolower(str[i]);
    }
    
    return str;
}

HT_API char * uppercase(char * str)
{
    uint32 i;
    
    for (i = 0; i < strlen(str); ++i)
    {
        str[i] = toupper(str[i]);
    }
    
    return str;
}

HT_API BOOL bin_to_hex_str(uint8 * bin, int binlen, char * hex, int hexlen)
{
    uint16 i;
    uint8 j;

    if (hexlen <= binlen*2)
    {
        return FALSE;
    }
    
    for (i = 0; i < binlen; i++) 
    {
        j = (bin[i] >> 4) & 0xf;

        if (j <= 9)
        {
            hex[i*2] = (j + '0');
        }    
        else
        {
            hex[i*2] = (j + 'a' - 10);
        }
        
        j = bin[i] & 0xf;

        if (j <= 9)
        {
            hex[i*2+1] = (j + '0');
        }    
        else
        {
            hex[i*2+1] = (j + 'a' - 10);
        }    
    };
    
    hex[binlen*2] = '\0';

    return TRUE;
};

HT_API int hex_str_to_bin(char * hex, int hexlen, uint8 * bin, int binlen)
{
    int i;

    if (binlen < hexlen/2)
    {
        return 0;
    }
    
    for (i=0; i<hexlen/2; i++)
    {
        if (hex[i*2] >= '0' && hex[i*2] <= '9')
        {
            bin[i] = (hex[i*2] - '0') << 4;
        }    
        else if (hex[i*2] >= 'a' && hex[i*2] <= 'z')
        {
            bin[i] = (hex[i*2] - 'a' + 10) << 4;
        }
        else if (hex[i*2] >= 'A' && hex[i*2] <= 'Z')
        {
            bin[i] = (hex[i*2] - 'A' + 10) << 4;
        }
        else
        {
            return 0;
        }
        
        if (hex[i*2+1] >= '0' && hex[i*2+1] <= '9')
        {
            bin[i] |= (hex[i*2+1] - '0');
        }    
        else if (hex[i*2+1] >= 'a' && hex[i*2+1] <= 'z')
        {
            bin[i] |= hex[i*2+1] - 'a' + 10;
        }
        else if (hex[i*2+1] >= 'A' && hex[i*2+1] <= 'Z')
        {
            bin[i] |= hex[i*2+1] - 'A' + 10;
        }
        else
        {
            return 0;
        }    
    }

    return i;
}

static char hextab[17] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 0};

HT_API int url_encode(const char * src, const int srcsize, char * dst, const int dstsize)  
{  
    int i;  
    int j;  
    char ch;  
  
    if ((NULL == src) || (NULL == dst) || (srcsize <= 0) || (dstsize <= 0)) 
    {  
        return 0;  
    }  
  
    for (i = 0, j = 0; i < srcsize && j < dstsize; ++i) 
    {  
        ch = src[i];  
        
        if (((ch >= 'A') && (ch <= 'Z')) ||  
            ((ch >= 'a') && (ch <= 'z')) ||  
            ((ch >= '0') && (ch <= '9')) || 
            ch == '.' || ch == '-' || ch == '_' || ch == '*') 
        {  
            dst[j++] = ch;  
        } 
        else if (ch == ' ') 
        {  
            dst[j++] = '+';  
        }
        else 
        {  
            if (j+3 < dstsize)
            {  
                dst[j] = '%';
                dst[j+1] = hextab[ch >> 4];  
                dst[j+2] = hextab[ch & 15];  

                j+=3;
            }
            else 
            {  
                return 0;  
            }  
        }  
    }  
  
    dst[j] = '\0';  
    
    return j;  
} 

HT_API int url_decode(char * dst, char const * src, uint32 len) 
{    
    char * p_dst = dst;
    const char * p_src = src;
    
    while (len > 0) 
    {
        int before = 0;
        int after = 0;

        if (*p_src == '+')   
        {  
            ++p_src;
            --len;
            *p_dst++ = ' ';
        }
        else if (*p_src == '%' && len >= 3 && sscanf(p_src+1, "%n%2hhx%n", &before, p_dst, &after) == 1) 
        {
            uint32 size = after - before; // should be 1 or 2

            ++p_dst;
            p_src += (1 + size);
            len -= (1 + size);
        } 
        else 
        {
            *p_dst++ = *p_src++;
            --len;
        }
    }
    
    *p_dst = '\0';

    return (int)(p_dst - dst);
}

void url_split(char const* url, char *proto, int proto_size,
    char *user, int user_size,
    char *pass, int pass_size,
    char *host, int host_size,
    int *port, char *path, int path_size)
{
    uint32 len;
    const char *p, *ls, *ls2, *at, *col, *brk;

    if (port)
        *port = -1;
    if (proto_size > 0)
        proto[0] = 0;
    if (user_size > 0)
        user[0] = 0;
    if (pass_size > 0)
        pass[0] = 0;
    if (host_size > 0)
        host[0] = 0;
    if (path_size > 0)
        path[0] = 0;

    /* parse protocol */
    if ((p = strchr(url, ':')))
    {
        if (proto)
        {
            len = MIN(proto_size - 1, p - url);
            strncpy(proto, url, len);
            proto[len] = '\0';
        }

        p++; /* skip ':' */
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
    }
    else if (path)
    {
        /* no protocol means plain filename */
        len = MIN(path_size - 1, (int)strlen(url));
        strncpy(path, url, len);
        path[len] = '\0';
        return;
    }

    if (NULL == p)
    {
        return;
    }

    /* separate path from hostname */
    ls = strchr(p, '/');
    ls2 = strchr(p, '?');
    if (!ls)
        ls = ls2;
    else if (ls2)
        ls = MIN(ls, ls2);

    if (ls)
    {
        if (path)
        {
            len = MIN(path_size - 1, (int)strlen(ls));
            strncpy(path, ls, len);
            path[len] = '\0';
        }
    }
    else
    {
        ls = &p[strlen(p)];  // XXX
    }

    /* the rest is hostname, use that to parse auth/port */
    if (ls != p)
    {
        /* authorization (user[:pass]@hostname) */

        while ((at = strchr(p, '@')) && at < ls)
        {
            if ((brk = strchr(p, ':')) && brk < ls)
            {
                if (user)
                {
                    url_decode(user, p, MIN(user_size-1, brk - p));
                }

                if (pass)
                {
                    url_decode(pass, brk + 1, MIN(pass_size-1, at - brk - 1));
                }
            }

            p = at + 1; /* skip '@' */
        }

        if (*p == '[' && (brk = strchr(p, ']')) && brk < ls)
        {
            /* [host]:port */
            if (host)
            {
                len = MIN(host_size - 1, brk - 1 - p);
                strncpy(host, p + 1, len);
                host[len] = '\0';
            }

            if (brk[1] == ':' && port)
            {
                *port = atoi(brk + 2);
            }
        }
        else if ((col = strchr(p, ':')) && col < ls)
        {
            if (host)
            {
                len = MIN(host_size - 1, col - p);
                strncpy(host, p, len);
                host[len] = '\0';
            }

            if (port)
            {
                *port = atoi(col + 1);
            }
        }
        else if (host)
        {
            len = MIN(host_size - 1, ls - p);
            strncpy(host, p, len);
            host[len] = '\0';
        }
    }
}

HT_API time_t get_time_by_string(char * p_time_str)
{
    char * ptr_s;
    struct tm st;
    
    memset(&st, 0, sizeof(struct tm));

    ptr_s = p_time_str;

    while (*ptr_s == ' ' || *ptr_s == '\t')
    {
        ptr_s++;
    }
    
    sscanf(ptr_s, "%04d-%02d-%02d %02d:%02d:%02d", 
        &st.tm_year, &st.tm_mon, &st.tm_mday, 
        &st.tm_hour, &st.tm_min, &st.tm_sec);

    st.tm_year -= 1900;
    st.tm_mon -= 1;

    return mktime(&st);
}

HT_API void get_time_str(char * buff, int len)
{
    time_t nowtime;
    struct tm *t1;    

    time(&nowtime);
    t1 = localtime(&nowtime);

    snprintf(buff, len, "%04d-%02d-%02d %02d:%02d:%02d",
        t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
        t1->tm_hour, t1->tm_min, t1->tm_sec);
}

HT_API void get_time_str_day_off(time_t nt, char * buff, int len, int dayoff)
{
    struct tm *t1;
    time_t nt1 = nt + dayoff * 24 * 3600;

    t1 = localtime(&nt1);

    snprintf(buff, len, "%04d-%02d-%02d %02d:%02d:%02d",
        t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
        t1->tm_hour, t1->tm_min, t1->tm_sec);
}

HT_API void get_time_str_mon_off(time_t nt, char * buff, int len, int moffset)
{
    struct tm *t1;
    int year;

    int day_of_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    t1 = localtime(&nt);

    t1->tm_mon += moffset;

    t1->tm_year += t1->tm_mon / 12;
    t1->tm_mon = t1->tm_mon % 12;

    year = t1->tm_year + 1900;
    
    if ((year % 400) == 0 || ((year % 100) != 0 && (year % 4) == 0))
    {
        day_of_month[1] = 29;
    }

    if (t1->tm_mday > day_of_month[t1->tm_mon])
    {
        t1->tm_mday = day_of_month[t1->tm_mon];
    }
    
    snprintf(buff, len, "%04d-%02d-%02d %02d:%02d:%02d",
        t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
        t1->tm_hour, t1->tm_min, t1->tm_sec);
}

HT_API time_t get_time_by_tstring(const char * p_time_str)
{
    const char * ptr_s;
    struct tm st;
    
    memset(&st, 0, sizeof(struct tm));

    ptr_s = p_time_str;
    
    while (*ptr_s == ' ' || *ptr_s == '\t') 
    {
        ptr_s++;
    }
    
    sscanf(ptr_s, "%04d-%02d-%02dT%02d:%02d:%02d", 
        &st.tm_year, &st.tm_mon, &st.tm_mday, 
        &st.tm_hour, &st.tm_min, &st.tm_sec);

    st.tm_year -= 1900;
    st.tm_mon -= 1;

    return mktime(&st);
}

HT_API void get_tstring_by_time(time_t t, char * buff, int len)
{
    struct tm *t1;

    t1 = localtime(&t);

    snprintf(buff, len, "%04d-%02d-%02dT%02d:%02d:%02d",
        t1->tm_year+1900, t1->tm_mon+1, t1->tm_mday,
        t1->tm_hour, t1->tm_min, t1->tm_sec);
}

HT_API SOCKET tcp_connect(const char * hostname, int port, int timeout)
{
    int ret;
    SOCKET fd;
    char portstr[10] = {'\0'};
    struct timeval tv;
    struct addrinfo hints, *ai, *cur_ai;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(portstr, sizeof(portstr), "%d", port);
    
    ret = getaddrinfo(hostname, portstr, &hints, &ai);
    if (ret) 
    {
        log_print(HT_LOG_ERR, "Failed to resolve hostname %s\r\n", hostname);
        return -1;
    }

    fd = -1;
    
    for (cur_ai = ai; cur_ai; cur_ai = cur_ai->ai_next) 
    {
        fd = socket(cur_ai->ai_family, cur_ai->ai_socktype, cur_ai->ai_protocol);
        if (fd <= 0)
        {
            continue;
        }

        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout%1000) * 1000;
        
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
        
        if (connect(fd, cur_ai->ai_addr, (int)(cur_ai->ai_addrlen)) < 0) 
        {
            closesocket(fd);
            fd = -1;

            log_print(HT_LOG_ERR, "Connect hostname %s failed\r\n", hostname);
            continue;
        } 

        break;  /* okay we got one */
    }

    freeaddrinfo(ai);
    
    return fd;   
}

HT_API SOCKET tcp_connect_timeout(struct sockaddr * addr, int timeout)
{
    size_t addrlen;
    SOCKET cfd;
    
#if __LINUX_OS__    
    uint32 starttime = sys_os_get_ms();
    struct timeval tv;
#elif __WINDOWS_OS__
    int flag = 0;
    unsigned long ul = 1;
#endif

    if (addr->sa_family == AF_INET)
    {
        addrlen = sizeof(struct sockaddr_in);
    }
    else if (addr->sa_family == AF_INET6)
    {
        addrlen = sizeof(struct sockaddr_in6);
    }
    else
    {
        return -1;
    }
    
    cfd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (cfd <= 0)
    {
        log_print(HT_LOG_ERR, "%s, socket failed\n", __FUNCTION__);
        return 0;
    }

#if __LINUX_OS__
    
    tv.tv_sec = timeout/1000;
    tv.tv_usec = (timeout%1000) * 1000;
    
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    while (connect(cfd, addr, addrlen) == -1 && errno != EISCONN)
    {
        if (sys_os_get_ms() > starttime + timeout)
        {
            closesocket(cfd);
            return -1;
        }
        
        if (errno != EINTR) 
        {
            closesocket(cfd);
            return -1;
        }
    }
    
    return cfd;
    
#elif __WINDOWS_OS__    
    
    ioctlsocket(cfd, FIONBIO, &ul);

    if (connect(cfd, addr, addrlen) != 0)
    {
        fd_set set;
        struct timeval tv;
        
        tv.tv_sec = timeout/1000;
        tv.tv_usec = (timeout%1000) * 1000;
        
        FD_ZERO(&set);
        FD_SET(cfd, &set);

        if (select((int)(cfd+1), NULL, &set, NULL, &tv) > 0)
        {
            int err = 0, len = sizeof(int);
            
            getsockopt(cfd, SOL_SOCKET, SO_ERROR, (char *)&err, (socklen_t*) &len);

            if (err == 0)
            {
                flag = 1;
            }    
        }
    }
    else
    {
        flag = 1;
    }

    ul = 0;
    ioctlsocket(cfd, FIONBIO, &ul);
        
    if (flag == 1)
    {
        return cfd;
    }
    else
    {
        closesocket(cfd);
        return 0;
    }

#endif    
}

HT_API void network_init()
{
#if __WINDOWS_OS__
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

HT_API int daemon_init()
{
#if __LINUX_OS__

    pid_t pid;

    pid = fork();
    
    if (pid == -1)
    {
        return -1;
    }    
    else if (pid > 0)
    {
        exit(0);
    }
    
    setsid();
    
#endif

    return 0;
}

#if __WINDOWS_OS__

#include <sys/timeb.h>

// used to make sure that static variables in gettimeofday() aren't 
//  initialized simultaneously by multiple threads
static long g_gettimeofday_lock = 0;  

HT_API int gettimeofday(struct timeval * tp, int * tz)
{
    static LARGE_INTEGER tickFrequency, epochOffset;

    static BOOL isInitialized = FALSE;

    LARGE_INTEGER tickNow;

    QueryPerformanceCounter(&tickNow);
 
    if (!isInitialized) 
    {
        if (1 == InterlockedIncrement(&g_gettimeofday_lock)) 
        {
            // For our first call, use "ftime()", so that we get a time with a proper epoch.
            // For subsequent calls, use "QueryPerformanceCount()", because it's more fine-grain.
            struct timeb tb;
            ftime(&tb);
            
            tp->tv_sec = (long)tb.time;
            tp->tv_usec = 1000*tb.millitm;

            // Also get our counter frequency:
            QueryPerformanceFrequency(&tickFrequency);

            // compute an offset to add to subsequent counter times, so we get a proper epoch:
            epochOffset.QuadPart = tp->tv_sec * tickFrequency.QuadPart + (tp->tv_usec * tickFrequency.QuadPart) / 1000000L - tickNow.QuadPart;

            // next caller can use ticks for time calculation
            isInitialized = TRUE; 
            
            return 0;
        } 
        else 
        {
            InterlockedDecrement(&g_gettimeofday_lock);
            
            // wait until first caller has initialized static values
            while (!isInitialized)
            {
                Sleep(1);
            }
        }
    }

    // adjust our tick count so that we get a proper epoch:
    tickNow.QuadPart += epochOffset.QuadPart;

    tp->tv_sec =  (long)(tickNow.QuadPart / tickFrequency.QuadPart);
    tp->tv_usec = (long)(((tickNow.QuadPart % tickFrequency.QuadPart) * 1000000L) / tickFrequency.QuadPart);

    return 0;
}

#endif


