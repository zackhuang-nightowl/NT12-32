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

#ifndef __H_UTIL_H__
#define __H_UTIL_H__


/*************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************/

#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

#define ARRAY_SIZE(ary)     (sizeof(ary) / sizeof(ary[0]))

typedef struct
{
    uint16  family;
    uint16  prefix_len;
    char    ip[65];
} HT_IFINFO;

typedef struct
{
    uint32      count;
    HT_IFINFO   table[1];
} HT_IFINFOTABLE;

typedef struct
{
    BOOL                ipv4_flag;
    BOOL                ipv6_flag;
    struct sockaddr_in  ipv4_addr;
    struct sockaddr_in6 ipv6_addr; 
} HT_SOCKADDR;

typedef enum
{
    HT_PROTOCOL_TCP,
    HT_PROTOCOL_UDP,
} HT_PROTOCOL;

/*************************************************************************/
HT_API BOOL         is_ipv4_address(const char * address);
HT_API BOOL         is_ipv6_address(const char * address);
HT_API BOOL         is_ip_address(const char * address);
HT_API BOOL         is_ipv4_local_address(struct in_addr * addr);
HT_API BOOL         is_ipv6_local_address(struct in6_addr * addr);
HT_API BOOL         is_local_address(struct sockaddr * addr);
HT_API BOOL         is_ipv4_linklocal_address(struct in_addr * addr);
HT_API BOOL         is_ipv6_linklocal_address(struct in6_addr * addr);
HT_API BOOL         is_linklocal_address(struct sockaddr * addr);
HT_API BOOL         is_same_ipv4_network(struct in_addr * src, struct in_addr * dst, int prefix_len);
HT_API BOOL         is_same_ipv6_network(struct in6_addr * src, struct in6_addr * dst, int prefix_len);
HT_API BOOL         is_local_ip_net(const char * ip);
HT_API int          get_ifinfo_table(HT_IFINFOTABLE * table, uint32 * size);
HT_API BOOL         get_route_if_ip(struct sockaddr * dst_ip, struct sockaddr * if_ip);
HT_API BOOL         get_default_if_ip(struct sockaddr * addr);
HT_API char       * get_local_ip(uint16 family, char * ip, int size);
HT_API BOOL         get_address_by_name(const char * host_name, HT_PROTOCOL protocol, HT_SOCKADDR * addr);
HT_API const char * get_default_gateway();
HT_API const char * get_dns_server();
HT_API int          get_mask_length_by_sockaddr(struct sockaddr * mask);
HT_API int          get_mask_length(const char * mask);
HT_API const char * get_mask_by_length(int family, int masklen, char * mask, int size);
HT_API int          get_sockaddr_port(struct sockaddr * addr);
HT_API const char * get_sockaddr_ip(struct sockaddr * addr, char * str, int str_len);

/*************************************************************************/
HT_API char       * lowercase(char * str);
HT_API char       * uppercase(char * str);

HT_API BOOL         bin_to_hex_str(uint8 * bin, int binlen, char * hex, int hexlen);
HT_API int          hex_str_to_bin(char * hex, int hexlen, uint8 * bin, int binlen);

HT_API int          url_encode(const char * src, const int srcsize, char * dst, const int dstsize);
HT_API int          url_decode(char * dst, char const * src, uint32 len);
HT_API void         url_split(char const* url, char *proto, int proto_size, char *user, int user_size, char *pass, 
                                 int pass_size, char *host, int host_size, int *port, char *path, int path_size);

/*************************************************************************/
HT_API time_t       get_time_by_string(char * p_time_str);
HT_API void         get_time_str(char * buff, int len);
HT_API void         get_time_str_day_off(time_t nt, char * buff, int len, int dayoff);
HT_API void         get_time_str_mon_off(time_t nt, char * buff, int len, int moffset);
HT_API time_t       get_time_by_tstring(const char * p_time_str);
HT_API void         get_tstring_by_time(time_t t, char * buff, int len);

HT_API SOCKET       tcp_connect_timeout(struct sockaddr * addr, int timeout);

/*************************************************************************/
HT_API void         network_init();
HT_API int          daemon_init();

#if __WINDOWS_OS__
HT_API int          gettimeofday(struct timeval* tp, int* tz);
#endif

#if defined(ANDROID)
HT_API void         freeifaddrs(struct ifaddrs *ifap);
HT_API int          getifaddrs(struct ifaddrs **ifap);
#endif

#ifdef __cplusplus
}
#endif

#endif  // __H_UTIL_H__



