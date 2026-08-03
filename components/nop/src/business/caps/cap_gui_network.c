/**
 * @file cap_gui_network.c
 * @brief CAP_NETWORK GUI network-settings handlers: DDNS, NTP, FTP, PoE, UPnP,
 *        network ports, email alert, LAN interface, local link, remote-access
 *        state and device UID. Gated by CAP_NETWORK. Backed by module-static
 *        state so config get/set round-trips. Scalar fields use static storage;
 *        the DDNS list is kept as a serialized JSON snapshot (stored via
 *        nop_json_print on set, parsed back via nop_json_parse on get) and falls
 *        back to a spec-shaped default when nothing has been set. Action/query
 *        commands (testEmailAlert, getLanInterface, getRemoteAccessState, getUID)
 *        return spec-shaped objects. Firmware can override these via a network
 *        HAL extension later.
 */
#include "business/business.h"
#include "base/nop_json.h"
#include "nop_sdk/nop_error_str.h"

#include <string.h>
#include <stdlib.h>

/* ---- DDNS list (object/array-valued; kept as a JSON snapshot) ------------- */
static char *g_ddns_info_json;                  /* {"DDNSInfo":[...]} snapshot */

/* ---- NTP ------------------------------------------------------------------ */
static bool g_ntp_enable                 = true;
static int  g_ntp_update_period          = 100;
static int  g_ntp_port                   = 123;
static char g_ntp_server_name[128]       = "pool.ntp.org";

/* ---- FTP ------------------------------------------------------------------ */
static bool g_ftp_enable                 = false;
static bool g_ftp_anonymous              = false;
static int  g_ftp_max_file_len           = 100;
static char g_ftp_password[128]          = "";
static int  g_ftp_port                   = 21;
static char g_ftp_remote_dir[256]        = "";
static char g_ftp_server[128]            = "";
static char g_ftp_user_name[128]         = "";

/* ---- PoE (per port, indexed from 1) --------------------------------------- */
#define GUI_NETWORK_MAX_POE_PORTS 16
static bool g_poe_enable[GUI_NETWORK_MAX_POE_PORTS];
static int  g_poe_power_used[GUI_NETWORK_MAX_POE_PORTS];

/* ---- UPnP ----------------------------------------------------------------- */
static bool g_upnp_enable                = true;
static int  g_upnp_http_port             = 81;
static int  g_upnp_tcp_port              = 9008;

/* ---- network ports -------------------------------------------------------- */
static int  g_net_http_port              = 80;
static int  g_net_https_port             = 443;
static int  g_net_tcp_port               = 9000;
static int  g_net_rtsp_port              = 554;

/* ---- email alert ---------------------------------------------------------- */
static bool g_email_enable               = false;
static char g_email_receiver1[256]       = "none";
static char g_email_receiver2[256]       = "none";
static char g_email_receiver3[256]       = "none";
static char g_email_receiver4[256]       = "none";
static char g_email_receiver5[256]       = "none";
static char g_email_sender[256]          = "";
static int  g_email_smtp_port            = 465;
static char g_email_smtp_server[256]     = "smtp.gmail.com";
static char g_email_user_name[256]       = "";
static char g_email_password[256]        = "";
static bool g_email_use_ssl              = true;
static char g_email_title[256]           = "";
static int  g_email_interval             = 600;

/* ---- local link ----------------------------------------------------------- */
static char g_link_network_type[16]      = "Static";
static char g_link_mac_addr[32]          = "00:60:B7:01:02:09";
static char g_link_ip_addr[64]           = "192.168.0.222";
static char g_link_subnet_mask[64]       = "255.255.255.0";
static char g_link_gateway[64]           = "192.168.0.1";
static char g_link_dns1[64]              = "202.96.128.86";
static char g_link_dns2[64]              = "202.96.134.133";

/* ---- remote access -------------------------------------------------------- */
static bool g_remote_access_enabled      = false;
static bool g_remote_access_bound_aws    = false;

/* ---- device identity ------------------------------------------------------ */
static char g_device_uid[64]             = "SKXGY35G8S7UST5X111A";
static char g_device_serial[64]          = "50GNMLE6IRGF";
static char g_device_mac[32]             = "54:2B:57:47:D0:5A";

/* Copy a string field into a fixed buffer with NUL termination. */
static void store_str(char *dst, size_t size, const char *src)
{
    if (!src)
        return;
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

/* ---- DDNS ----------------------------------------------------------------- */
/* Append one spec-default DDNS entry to @p arr. */
static void ddns_append_default(nop_json_t *arr, const char *host, const char *key,
                                bool enable)
{
    nop_json_t *entry = nop_json_obj();
    nop_json_add_str(entry, "Domain", "");
    nop_json_add_bool(entry, "Enable", enable);
    nop_json_add_str(entry, "Password", "");
    nop_json_add_str(entry, "HostName", host);
    nop_json_add_str(entry, "DDNSKey", key);
    nop_json_add_str(entry, "UserName", "");
    nop_json_arr_push(arr, entry);
}

static nop_status_t handle_get_ddns(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    nop_json_t *list;
    (void)request; (void)handler_context;

    if (g_ddns_info_json) {
        response->content = nop_json_parse(g_ddns_info_json,
                                           strlen(g_ddns_info_json));
        if (response->content)
            return NOP_OK;
    }
    response->content = nop_json_obj();
    list = nop_json_arr();
    ddns_append_default(list, "your.gicp.net",  "Orey",    false);
    ddns_append_default(list, "your.dyndns.org", "DynDNS",  true);
    ddns_append_default(list, "your.dyndns.org", "CN99",    false);
    ddns_append_default(list, "your.dyndns.org", "MYO-SEE", false);
    ddns_append_default(list, "your.dyndns.org", "NO-IP",   false);
    nop_json_add(response->content, "DDNSInfo", list);
    return NOP_OK;
}

static nop_status_t handle_set_ddns(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const nop_json_t *opt;
    char *text;
    nop_json_t *snapshot, *list;
    (void)response; (void)handler_context;

    opt = nop_json_get(request->args, "DDNSOpt");
    if (!nop_json_is_arr(opt))
        return NOP_ERR_PARAM;

    /* Re-serialize the supplied list under the get-shaped "DDNSInfo" key. */
    text = nop_json_print(opt);
    if (!text)
        return NOP_ERR_PARAM;
    list = nop_json_parse(text, strlen(text));
    free(text);
    if (!list)
        return NOP_ERR_PARAM;

    snapshot = nop_json_obj();
    nop_json_add(snapshot, "DDNSInfo", list);
    text = nop_json_print(snapshot);
    nop_json_free(snapshot);
    if (!text)
        return NOP_ERR_PARAM;
    free(g_ddns_info_json);
    g_ddns_info_json = text;
    return NOP_OK;
}

/* ---- NTP ------------------------------------------------------------------ */
static nop_status_t handle_get_ntp(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    nop_json_t *info, *server_option;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    info = nop_json_obj();
    nop_json_add_bool(info, "Enable", g_ntp_enable);
    nop_json_add_int(info, "UpdatePeriod", g_ntp_update_period);
    nop_json_add_int(info, "Port", g_ntp_port);
    nop_json_add_str(info, "ServerName", g_ntp_server_name);
    server_option = nop_json_obj();
    nop_json_add_str(server_option, "Option1", "pool.ntp.org");
    nop_json_add_str(server_option, "Option2", "time.windows.com");
    nop_json_add_str(server_option, "Option3", "");
    nop_json_add(info, "ServerOption", server_option);
    nop_json_add(response->content, "NTPInfo", info);
    return NOP_OK;
}

static nop_status_t handle_set_ntp(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    const nop_json_t *info;
    (void)response; (void)handler_context;
    info = nop_json_get(request->args, "NTPInfo");
    if (!info)
        return NOP_ERR_PARAM;
    g_ntp_enable        = nop_json_bool(info, "Enable", g_ntp_enable);
    g_ntp_update_period = (int)nop_json_num(info, "UpdatePeriod", g_ntp_update_period);
    g_ntp_port          = (int)nop_json_num(info, "Port", g_ntp_port);
    store_str(g_ntp_server_name, sizeof(g_ntp_server_name),
              nop_json_str(info, "ServerName", g_ntp_server_name));
    return NOP_OK;
}

/* ---- FTP ------------------------------------------------------------------ */
static nop_status_t handle_get_ftp(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    nop_json_t *info;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    info = nop_json_obj();
    nop_json_add_bool(info, "Enable", g_ftp_enable);
    nop_json_add_bool(info, "Anonymous", g_ftp_anonymous);
    nop_json_add_int(info, "MaxFileLen", g_ftp_max_file_len);
    nop_json_add_str(info, "Password", g_ftp_password);
    nop_json_add_int(info, "Port", g_ftp_port);
    nop_json_add_str(info, "RemoteDir", g_ftp_remote_dir);
    nop_json_add_str(info, "Server", g_ftp_server);
    nop_json_add_str(info, "UserName", g_ftp_user_name);
    nop_json_add(response->content, "FtpInfo", info);
    return NOP_OK;
}

static nop_status_t handle_set_ftp(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    const nop_json_t *info;
    (void)response; (void)handler_context;
    info = nop_json_get(request->args, "FtpInfo");
    if (!info)
        return NOP_ERR_PARAM;
    g_ftp_enable       = nop_json_bool(info, "Enable", g_ftp_enable);
    g_ftp_anonymous    = nop_json_bool(info, "Anonymous", g_ftp_anonymous);
    g_ftp_max_file_len = (int)nop_json_num(info, "MaxFileLen", g_ftp_max_file_len);
    g_ftp_port         = (int)nop_json_num(info, "Port", g_ftp_port);
    store_str(g_ftp_password, sizeof(g_ftp_password),
              nop_json_str(info, "Password", g_ftp_password));
    store_str(g_ftp_remote_dir, sizeof(g_ftp_remote_dir),
              nop_json_str(info, "RemoteDir", g_ftp_remote_dir));
    store_str(g_ftp_server, sizeof(g_ftp_server),
              nop_json_str(info, "Server", g_ftp_server));
    store_str(g_ftp_user_name, sizeof(g_ftp_user_name),
              nop_json_str(info, "UserName", g_ftp_user_name));
    return NOP_OK;
}

/* ---- PoE ------------------------------------------------------------------ */
static nop_status_t handle_get_poe(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    int channel;
    (void)handler_context;
    if (!nop_json_has(request->args, "channel"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    response->content = nop_json_obj();
    if (channel >= 1 && channel <= GUI_NETWORK_MAX_POE_PORTS) {
        nop_json_add_bool(response->content, "Enable", g_poe_enable[channel - 1]);
        nop_json_add_int(response->content, "PowerUsed", g_poe_power_used[channel - 1]);
    } else {
        nop_json_add_bool(response->content, "Enable", false);
        nop_json_add_int(response->content, "PowerUsed", 0);
    }
    return NOP_OK;
}

static nop_status_t handle_set_poe(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    int channel;
    bool enable;
    (void)response; (void)handler_context;
    if (!nop_json_has(request->args, "channel") ||
        !nop_json_has(request->args, "Enable"))
        return NOP_ERR_PARAM;
    channel = (int)nop_json_num(request->args, "channel", 0);
    if (channel < 1 || channel > GUI_NETWORK_MAX_POE_PORTS)
        return NOP_ERR_PARAM;
    enable = nop_json_bool(request->args, "Enable", false);
    g_poe_enable[channel - 1]     = enable;
    g_poe_power_used[channel - 1] = enable ? 5 : 0;
    return NOP_OK;
}

/* ---- UPnP ----------------------------------------------------------------- */
static nop_status_t handle_get_upnp(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    nop_json_t *info;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    info = nop_json_obj();
    nop_json_add_bool(info, "Enable", g_upnp_enable);
    nop_json_add_int(info, "HTTPPort", g_upnp_http_port);
    nop_json_add_int(info, "TCPPort", g_upnp_tcp_port);
    nop_json_add(response->content, "UPnPInfo", info);
    return NOP_OK;
}

static nop_status_t handle_set_upnp(const nop_request_t *request,
                                    nop_response_t *response,
                                    void *handler_context)
{
    const nop_json_t *enable;
    (void)response; (void)handler_context;
    enable = nop_json_get(request->args, "UPnPEnable");
    if (!enable || !nop_json_has(enable, "Enable"))
        return NOP_ERR_PARAM;
    g_upnp_enable = nop_json_bool(enable, "Enable", g_upnp_enable);
    return NOP_OK;
}

/* ---- network ports -------------------------------------------------------- */
static nop_status_t handle_get_net_port(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    nop_json_t *info;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    info = nop_json_obj();
    nop_json_add_int(info, "HttpPort", g_net_http_port);
    nop_json_add_int(info, "HttpsPort", g_net_https_port);
    nop_json_add_int(info, "TCPPort", g_net_tcp_port);
    nop_json_add_int(info, "RtspPort", g_net_rtsp_port);
    nop_json_add(response->content, "NetPortInfo", info);
    return NOP_OK;
}

static nop_status_t handle_set_net_port(const nop_request_t *request,
                                        nop_response_t *response,
                                        void *handler_context)
{
    const nop_json_t *info;
    (void)response; (void)handler_context;
    info = nop_json_get(request->args, "NetPortInfo");
    if (!info)
        return NOP_ERR_PARAM;
    g_net_http_port  = (int)nop_json_num(info, "HttpPort", g_net_http_port);
    g_net_https_port = (int)nop_json_num(info, "HttpsPort", g_net_https_port);
    g_net_tcp_port   = (int)nop_json_num(info, "TCPPort", g_net_tcp_port);
    g_net_rtsp_port  = (int)nop_json_num(info, "RtspPort", g_net_rtsp_port);
    return NOP_OK;
}

/* ---- email alert ---------------------------------------------------------- */
static nop_status_t handle_get_email_alert(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    nop_json_t *opt, *server_opt;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    opt = nop_json_obj();
    nop_json_add_bool(opt, "Enable", g_email_enable);
    nop_json_add_str(opt, "Receiver1", g_email_receiver1);
    nop_json_add_str(opt, "Receiver2", g_email_receiver2);
    nop_json_add_str(opt, "Receiver3", g_email_receiver3);
    nop_json_add_str(opt, "Receiver4", g_email_receiver4);
    nop_json_add_str(opt, "Receiver5", g_email_receiver5);
    nop_json_add_str(opt, "Sender", g_email_sender);
    nop_json_add_int(opt, "SMTPPort", g_email_smtp_port);
    nop_json_add_str(opt, "SMPTServer", g_email_smtp_server);
    server_opt = nop_json_obj();
    nop_json_add_str(server_opt, "SMPTServer1", "smtp.gmail.com");
    nop_json_add_str(server_opt, "SMPTServer2", "smtp.mail.yahoo.com");
    nop_json_add_str(server_opt, "SMPTServer3", "smtp.live.com");
    nop_json_add_str(server_opt, "SMPTServer4", "smtp.163.com");
    nop_json_add_str(server_opt, "SMPTServer5", "smtp.126.com");
    nop_json_add_str(server_opt, "SMPTServer6", "smtp.qq.com");
    nop_json_add_str(server_opt, "SMPTServer7", "smtp.foxmail.com");
    nop_json_add(opt, "SMPTServerOpt", server_opt);
    nop_json_add_str(opt, "UserName", g_email_user_name);
    nop_json_add_str(opt, "Password", g_email_password);
    nop_json_add_bool(opt, "UseSSL", g_email_use_ssl);
    nop_json_add_str(opt, "Title", g_email_title);
    nop_json_add_int(opt, "Interval", g_email_interval);
    nop_json_add(response->content, "EmailAlertOpt", opt);
    return NOP_OK;
}

static nop_status_t handle_set_email_alert(const nop_request_t *request,
                                           nop_response_t *response,
                                           void *handler_context)
{
    const nop_json_t *opt;
    (void)response; (void)handler_context;
    opt = nop_json_get(request->args, "EmailAlertOpt");
    if (!opt)
        return NOP_ERR_PARAM;
    g_email_enable    = nop_json_bool(opt, "Enable", g_email_enable);
    g_email_smtp_port = (int)nop_json_num(opt, "SMTPPort", g_email_smtp_port);
    g_email_use_ssl   = nop_json_bool(opt, "UseSSL", g_email_use_ssl);
    g_email_interval  = (int)nop_json_num(opt, "Interval", g_email_interval);
    store_str(g_email_receiver1, sizeof(g_email_receiver1),
              nop_json_str(opt, "Receiver1", g_email_receiver1));
    store_str(g_email_receiver2, sizeof(g_email_receiver2),
              nop_json_str(opt, "Receiver2", g_email_receiver2));
    store_str(g_email_receiver3, sizeof(g_email_receiver3),
              nop_json_str(opt, "Receiver3", g_email_receiver3));
    store_str(g_email_receiver4, sizeof(g_email_receiver4),
              nop_json_str(opt, "Receiver4", g_email_receiver4));
    store_str(g_email_receiver5, sizeof(g_email_receiver5),
              nop_json_str(opt, "Receiver5", g_email_receiver5));
    store_str(g_email_sender, sizeof(g_email_sender),
              nop_json_str(opt, "Sender", g_email_sender));
    store_str(g_email_smtp_server, sizeof(g_email_smtp_server),
              nop_json_str(opt, "SMPTServer", g_email_smtp_server));
    store_str(g_email_user_name, sizeof(g_email_user_name),
              nop_json_str(opt, "UserName", g_email_user_name));
    store_str(g_email_password, sizeof(g_email_password),
              nop_json_str(opt, "Password", g_email_password));
    store_str(g_email_title, sizeof(g_email_title),
              nop_json_str(opt, "Title", g_email_title));
    return NOP_OK;
}

static nop_status_t handle_test_email_alert(const nop_request_t *request,
                                            nop_response_t *response,
                                            void *handler_context)
{
    const nop_json_t *opt;
    (void)handler_context;
    opt = nop_json_get(request->args, "testEmailAlert");
    if (!opt)
        return NOP_ERR_PARAM;
    /* No SMTP transport wired yet — report a well-formed success result. */
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "rspCode", 200);
    return NOP_OK;
}

/* ---- LAN interface -------------------------------------------------------- */
static nop_status_t handle_get_lan_interface(const nop_request_t *request,
                                             nop_response_t *response,
                                             void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_int(response->content, "totalPhysicalBandwidth", 100);
    nop_json_add_int(response->content, "maxRxBandwidth", 100);
    nop_json_add_int(response->content, "allocatedRxBandwidth", 0);
    return NOP_OK;
}

/* ---- local link ----------------------------------------------------------- */
static nop_status_t handle_get_local_link(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    nop_json_t *link;
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    link = nop_json_obj();
    nop_json_add_str(link, "networkType", g_link_network_type);
    nop_json_add_str(link, "macAddr", g_link_mac_addr);
    nop_json_add_str(link, "ipAddr", g_link_ip_addr);
    nop_json_add_str(link, "subnetMask", g_link_subnet_mask);
    nop_json_add_str(link, "gateway", g_link_gateway);
    nop_json_add_str(link, "DNS1", g_link_dns1);
    nop_json_add_str(link, "DNS2", g_link_dns2);
    nop_json_add(response->content, "LocalLink", link);
    return NOP_OK;
}

static nop_status_t handle_set_local_link(const nop_request_t *request,
                                          nop_response_t *response,
                                          void *handler_context)
{
    const nop_json_t *link;
    (void)response; (void)handler_context;
    link = nop_json_get(request->args, "LocalLink");
    if (!link)
        return NOP_ERR_PARAM;
    store_str(g_link_network_type, sizeof(g_link_network_type),
              nop_json_str(link, "networkType", g_link_network_type));
    store_str(g_link_ip_addr, sizeof(g_link_ip_addr),
              nop_json_str(link, "ipAddr", g_link_ip_addr));
    store_str(g_link_subnet_mask, sizeof(g_link_subnet_mask),
              nop_json_str(link, "subnetMask", g_link_subnet_mask));
    store_str(g_link_gateway, sizeof(g_link_gateway),
              nop_json_str(link, "gateway", g_link_gateway));
    store_str(g_link_dns1, sizeof(g_link_dns1),
              nop_json_str(link, "DNS1", g_link_dns1));
    store_str(g_link_dns2, sizeof(g_link_dns2),
              nop_json_str(link, "DNS2", g_link_dns2));
    return NOP_OK;
}

/* ---- remote access -------------------------------------------------------- */
static nop_status_t handle_get_remote_access_state(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_bool(response->content, "enabled", g_remote_access_enabled);
    nop_json_add_bool(response->content, "isBoundAws", g_remote_access_bound_aws);
    return NOP_OK;
}

static nop_status_t handle_set_remote_access_state(const nop_request_t *request,
                                                   nop_response_t *response,
                                                   void *handler_context)
{
    (void)handler_context;
    if (!nop_json_has(request->args, "enable"))
        return NOP_ERR_PARAM;
    response->content = nop_json_obj();
    if (g_remote_access_bound_aws) {
        nop_json_add_str(response->content, "result", NOP_RESULT_ALREADY_BOUND);
        return NOP_OK;
    }
    g_remote_access_enabled = nop_json_bool(request->args, "enable",
                                            g_remote_access_enabled);
    nop_json_add_str(response->content, "result", "OK");
    return NOP_OK;
}

/* ---- device identity ------------------------------------------------------ */
static nop_status_t handle_get_uid(const nop_request_t *request,
                                   nop_response_t *response,
                                   void *handler_context)
{
    (void)request; (void)handler_context;
    response->content = nop_json_obj();
    nop_json_add_str(response->content, "uid", g_device_uid);
    nop_json_add_str(response->content, "serial", g_device_serial);
    nop_json_add_str(response->content, "mac_address", g_device_mac);
    return NOP_OK;
}

void cap_gui_network_register(nop_router_t *router)
{
    nop_router_register(router, "GUI_getDDNS", CAP_NETWORK, handle_get_ddns);
    nop_router_register(router, "GUI_setDDNS", CAP_NETWORK, handle_set_ddns);
    nop_router_register(router, "GUI_getNTP", CAP_NETWORK, handle_get_ntp);
    nop_router_register(router, "GUI_setNTP", CAP_NETWORK, handle_set_ntp);
    nop_router_register(router, "GUI_getFTP", CAP_NETWORK, handle_get_ftp);
    nop_router_register(router, "GUI_setFTP", CAP_NETWORK, handle_set_ftp);
    nop_router_register(router, "GUI_getPoE", CAP_NETWORK, handle_get_poe);
    nop_router_register(router, "GUI_setPoE", CAP_NETWORK, handle_set_poe);
    nop_router_register(router, "GUI_getUPnP", CAP_NETWORK, handle_get_upnp);
    nop_router_register(router, "GUI_setUPnP", CAP_NETWORK, handle_set_upnp);
    nop_router_register(router, "GUI_getNetPort", CAP_NETWORK, handle_get_net_port);
    nop_router_register(router, "GUI_setNetPort", CAP_NETWORK, handle_set_net_port);
    nop_router_register(router, "GUI_getEmailAlert", CAP_NETWORK, handle_get_email_alert);
    nop_router_register(router, "GUI_setEmailAlert", CAP_NETWORK, handle_set_email_alert);
    nop_router_register(router, "GUI_testEmailAlert", CAP_NETWORK, handle_test_email_alert);
    nop_router_register(router, "GUI_getLanInterface", CAP_NETWORK, handle_get_lan_interface);
    nop_router_register(router, "GUI_getLocalLink", CAP_NETWORK, handle_get_local_link);
    nop_router_register(router, "GUI_setLocalLink", CAP_NETWORK, handle_set_local_link);
    nop_router_register(router, "GUI_getRemoteAccessState", CAP_NETWORK,
                        handle_get_remote_access_state);
    nop_router_register(router, "GUI_setRemoteAccessState", CAP_NETWORK,
                        handle_set_remote_access_state);
    nop_router_register(router, "GUI_getUID", CAP_NETWORK, handle_get_uid);
}
