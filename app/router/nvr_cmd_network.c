/***************************************************************************************
 *  nvr_cmd_network.c — 网络/时间 LOCAL handler。
 *  local_link/WAN/LAN/NetPort/UPnP/PoE/Email/NTP/DDNS/FTP — Linux 系统 + nvr_settings。
 **************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_netime.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "nvr_channel.h"
#include <string.h>
#include <stdio.h>

static void local_link_to_json(const nvr_local_link_t *lk, cJSON *o)
{
    cJSON *link = cJSON_CreateObject();
    cJSON_AddStringToObject(link, "networkType", lk->network_type);
    cJSON_AddStringToObject(link, "macAddr", lk->mac);
    cJSON_AddStringToObject(link, "ipAddr", lk->ip);
    cJSON_AddStringToObject(link, "subnetMask", lk->subnet_mask);
    cJSON_AddStringToObject(link, "gateway", lk->gateway);
    cJSON_AddStringToObject(link, "DNS1", lk->dns1);
    cJSON_AddStringToObject(link, "DNS2", lk->dns2);
    cJSON_AddItemToObject(o, "LocalLink", link);
}

char *cmd_GUI_getLocalLink(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    nvr_local_link_t lk;
    if (!c->settings || nvr_net_local_link_fill(c->settings, &lk) != 0)
        return nvr_resp_err("no_settings");
    cJSON *o = cJSON_CreateObject();
    local_link_to_json(&lk, o);
    return nvr_resp_content(o);
}

char *cmd_GUI_setLocalLink(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *link = a ? cJSON_GetObjectItem(a, "LocalLink") : NULL;
    if (!cJSON_IsObject(link) || !c->settings) return nvr_resp_err("invalid_param");

    nvr_local_link_t lk;
    if (nvr_net_local_link_fill(c->settings, &lk) != 0)
        memset(&lk, 0, sizeof(lk));

    const char *v;
    if ((v = nvr_jstr(link, "networkType", NULL))) snprintf(lk.network_type, sizeof(lk.network_type), "%s", v);
    if ((v = nvr_jstr(link, "ipAddr", NULL)))      snprintf(lk.ip, sizeof(lk.ip), "%s", v);
    if ((v = nvr_jstr(link, "subnetMask", NULL)))  snprintf(lk.subnet_mask, sizeof(lk.subnet_mask), "%s", v);
    if ((v = nvr_jstr(link, "gateway", NULL)))     snprintf(lk.gateway, sizeof(lk.gateway), "%s", v);
    if ((v = nvr_jstr(link, "DNS1", NULL)))        snprintf(lk.dns1, sizeof(lk.dns1), "%s", v);
    if ((v = nvr_jstr(link, "DNS2", NULL)))        snprintf(lk.dns2, sizeof(lk.dns2), "%s", v);

    if (nvr_net_local_link_apply(c->settings, &lk) != 0)
        return nvr_resp_err("apply_failed");
    NVR_LOGI("router", "setLocalLink type=%s ip=%s", lk.network_type, lk.ip);
    return nvr_resp_ok();
}

static char *wan_interface_resp(void)
{
    nvr_wan_if_info_t wan;
    if (nvr_net_wan_fill(&wan) != 0)
        return nvr_resp_err("wan_unavailable");

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "value", wan.value);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    for (int i = 0; i < wan.list_n; i++)
        cJSON_AddItemToArray(list, cJSON_CreateString(wan.list[i]));
    cJSON *conn = cJSON_AddArrayToObject(o, "connected");
    for (int i = 0; i < wan.conn_n; i++)
        cJSON_AddItemToArray(conn, cJSON_CreateString(wan.connected[i]));
    return nvr_resp_content(o);
}

char *cmd_GUI_getWanInterface(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return wan_interface_resp();
}

char *cmd_getWanInterface(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    return wan_interface_resp();
}

char *cmd_GUI_getLanInterface(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a; (void)c;
    int total = 0, max_rx = 0;
    if (nvr_net_lan_bandwidth_mbps(&total, &max_rx) != 0)
        return nvr_resp_err("link_speed_unavailable");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "totalPhysicalBandwidth", total);
    cJSON_AddNumberToObject(o, "maxRxBandwidth", max_rx);
    /* allocatedRxBandwidth: 无流统计来源，待实现；不返回假 0 */
    return nvr_resp_content(o);
}

char *cmd_GUI_getNetPort(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    cJSON *o = cJSON_CreateObject();
    cJSON *info = cJSON_CreateObject();
    cJSON_AddNumberToObject(info, "HttpPort",  nvr_settings_get_int(c->settings, "network.port.http",  80));
    cJSON_AddNumberToObject(info, "HttpsPort", nvr_settings_get_int(c->settings, "network.port.https", 443));
    cJSON_AddNumberToObject(info, "TCPPort",   nvr_settings_get_int(c->settings, "network.port.tcp",   NVR_DEF_NOP_PORT));
    cJSON_AddNumberToObject(info, "RtspPort",  nvr_settings_get_int(c->settings, "network.port.rtsp",  554));
    cJSON_AddItemToObject(o, "NetPortInfo", info);
    return nvr_resp_content(o);
}

char *cmd_GUI_setNetPort(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *info = a ? cJSON_GetObjectItem(a, "NetPortInfo") : NULL;
    if (!cJSON_IsObject(info) || !c->settings) return nvr_resp_err("invalid_param");
    if (nvr_jhas(info, "HttpPort"))
        nvr_settings_set_int(c->settings, "network.port.http", nvr_jint(info, "HttpPort", 80));
    if (nvr_jhas(info, "HttpsPort"))
        nvr_settings_set_int(c->settings, "network.port.https", nvr_jint(info, "HttpsPort", 443));
    if (nvr_jhas(info, "TCPPort"))
        nvr_settings_set_int(c->settings, "network.port.tcp", nvr_jint(info, "TCPPort", NVR_DEF_NOP_PORT));
    if (nvr_jhas(info, "RtspPort"))
        nvr_settings_set_int(c->settings, "network.port.rtsp", nvr_jint(info, "RtspPort", 554));
    NVR_LOGI("router", "setNetPort http=%d tcp=%d",
             nvr_settings_get_int(c->settings, "network.port.http", 80),
             nvr_settings_get_int(c->settings, "network.port.tcp", NVR_DEF_NOP_PORT));
    return nvr_resp_ok();
}

char *cmd_GUI_getNTP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    char srv[128];
    nvr_settings_get_str(c->settings, "system.ntp", srv, sizeof(srv), "pool.ntp.org");
    int en = nvr_settings_get_int(c->settings, "system.time_sync", 1);
    cJSON *o = cJSON_CreateObject();
    cJSON *info = cJSON_CreateObject();
    cJSON_AddBoolToObject(info, "Enable", en);
    cJSON_AddNumberToObject(info, "UpdatePeriod", 100);
    cJSON_AddNumberToObject(info, "Port", 123);
    cJSON_AddStringToObject(info, "ServerName", srv);
    cJSON *opt = cJSON_CreateObject();
    cJSON_AddStringToObject(opt, "Option1", "pool.ntp.org");
    cJSON_AddStringToObject(opt, "Option2", "time.windows.com");
    cJSON_AddStringToObject(opt, "Option3", "");
    cJSON_AddItemToObject(info, "ServerOption", opt);
    cJSON_AddItemToObject(o, "NTPInfo", info);
    return nvr_resp_content(o);
}

char *cmd_GUI_setNTP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *info = a ? cJSON_GetObjectItem(a, "NTPInfo") : NULL;
    if (!cJSON_IsObject(info)) return nvr_resp_err("invalid_param");
    if (nvr_jhas(info, "Enable"))
        nvr_settings_set_int(c->settings, "system.time_sync", nvr_jbool(info, "Enable", 1));
    const char *srv = nvr_jstr(info, "ServerName", NULL);
    if (srv && srv[0]) nvr_settings_set_str(c->settings, "system.ntp", srv);
    nvr_time_resync(c->settings);
    return nvr_resp_ok();
}

static void email_defaults(nvr_email_cfg_t *e)
{
    memset(e, 0, sizeof(*e));
    snprintf(e->receiver[0], sizeof(e->receiver[0]), "none");
    snprintf(e->sender, sizeof(e->sender), "");
    e->smtp_port = 465;
    snprintf(e->smtp_server, sizeof(e->smtp_server), "smtp.gmail.com");
    e->use_ssl = 1;
    e->interval = 600;
}

char *cmd_GUI_getEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    nvr_email_cfg_t e; email_defaults(&e);
    if (c->settings) nvr_settings_email_get(c->settings, &e);
    cJSON *o = cJSON_CreateObject();
    cJSON *opt = cJSON_CreateObject();
    cJSON_AddBoolToObject(opt, "Enable", e.enable);
    cJSON_AddStringToObject(opt, "Receiver1", e.receiver[0]);
    cJSON_AddStringToObject(opt, "Receiver2", e.receiver[1][0] ? e.receiver[1] : "none");
    cJSON_AddStringToObject(opt, "Receiver3", e.receiver[2][0] ? e.receiver[2] : "none");
    cJSON_AddStringToObject(opt, "Receiver4", e.receiver[3][0] ? e.receiver[3] : "none");
    cJSON_AddStringToObject(opt, "Receiver5", e.receiver[4][0] ? e.receiver[4] : "none");
    cJSON_AddStringToObject(opt, "Sender", e.sender);
    cJSON_AddNumberToObject(opt, "SMTPPort", e.smtp_port);
    cJSON_AddStringToObject(opt, "SMPTServer", e.smtp_server);
    cJSON_AddStringToObject(opt, "UserName", e.username);
    cJSON_AddStringToObject(opt, "Password", e.password);
    cJSON_AddBoolToObject(opt, "UseSSL", e.use_ssl);
    cJSON_AddStringToObject(opt, "Title", e.title);
    cJSON_AddNumberToObject(opt, "Interval", e.interval);
    cJSON_AddItemToObject(o, "EmailAlertOpt", opt);
    return nvr_resp_content(o);
}

char *cmd_GUI_setEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *opt = a ? cJSON_GetObjectItem(a, "EmailAlertOpt") : NULL;
    if (!cJSON_IsObject(opt)) return nvr_resp_err("invalid_param");
    nvr_email_cfg_t e; email_defaults(&e);
    if (c->settings) nvr_settings_email_get(c->settings, &e);
    e.enable = nvr_jbool(opt, "Enable", e.enable);
    e.smtp_port = nvr_jint(opt, "SMTPPort", e.smtp_port);
    e.use_ssl = nvr_jbool(opt, "UseSSL", e.use_ssl);
    e.interval = nvr_jint(opt, "Interval", e.interval);
    snprintf(e.receiver[0], sizeof(e.receiver[0]), "%s", nvr_jstr(opt, "Receiver1", "none"));
    snprintf(e.receiver[1], sizeof(e.receiver[1]), "%s", nvr_jstr(opt, "Receiver2", "none"));
    snprintf(e.receiver[2], sizeof(e.receiver[2]), "%s", nvr_jstr(opt, "Receiver3", "none"));
    snprintf(e.receiver[3], sizeof(e.receiver[3]), "%s", nvr_jstr(opt, "Receiver4", "none"));
    snprintf(e.receiver[4], sizeof(e.receiver[4]), "%s", nvr_jstr(opt, "Receiver5", "none"));
    snprintf(e.sender, sizeof(e.sender), "%s", nvr_jstr(opt, "Sender", ""));
    snprintf(e.smtp_server, sizeof(e.smtp_server), "%s", nvr_jstr(opt, "SMPTServer", e.smtp_server));
    snprintf(e.username, sizeof(e.username), "%s", nvr_jstr(opt, "UserName", ""));
    snprintf(e.password, sizeof(e.password), "%s", nvr_jstr(opt, "Password", ""));
    snprintf(e.title, sizeof(e.title), "%s", nvr_jstr(opt, "Title", ""));
    if (c->settings && nvr_settings_email_set(c->settings, &e) != 0)
        return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_GUI_getFTP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    nvr_ftp_cfg_t f; memset(&f, 0, sizeof(f));
    f.port = 21;
    if (c->settings) nvr_settings_ftp_get(c->settings, &f);
    cJSON *o = cJSON_CreateObject();
    cJSON *info = cJSON_CreateObject();
    cJSON_AddBoolToObject(info, "Enable", f.enable);
    cJSON_AddBoolToObject(info, "Anonymous", f.anonymous);
    cJSON_AddNumberToObject(info, "MaxFileLen", f.max_file_len ? f.max_file_len : 100);
    cJSON_AddStringToObject(info, "Password", f.password);
    cJSON_AddNumberToObject(info, "Port", f.port);
    cJSON_AddStringToObject(info, "RemoteDir", f.remote_dir);
    cJSON_AddStringToObject(info, "Server", f.server);
    cJSON_AddStringToObject(info, "UserName", f.username);
    cJSON_AddItemToObject(o, "FtpInfo", info);
    return nvr_resp_content(o);
}

char *cmd_GUI_setFTP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *info = a ? cJSON_GetObjectItem(a, "FtpInfo") : NULL;
    if (!cJSON_IsObject(info)) return nvr_resp_err("invalid_param");
    nvr_ftp_cfg_t f; memset(&f, 0, sizeof(f)); f.port = 21;
    if (c->settings) nvr_settings_ftp_get(c->settings, &f);
    f.enable = nvr_jbool(info, "Enable", f.enable);
    f.anonymous = nvr_jbool(info, "Anonymous", f.anonymous);
    f.max_file_len = nvr_jint(info, "MaxFileLen", f.max_file_len ? f.max_file_len : 100);
    f.port = nvr_jint(info, "Port", f.port);
    snprintf(f.password, sizeof(f.password), "%s", nvr_jstr(info, "Password", ""));
    snprintf(f.remote_dir, sizeof(f.remote_dir), "%s", nvr_jstr(info, "RemoteDir", ""));
    snprintf(f.server, sizeof(f.server), "%s", nvr_jstr(info, "Server", ""));
    snprintf(f.username, sizeof(f.username), "%s", nvr_jstr(info, "UserName", ""));
    if (c->settings && nvr_settings_ftp_set(c->settings, &f) != 0)
        return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_GUI_getDDNS(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    nvr_ddns_row_t rows[8]; int n = c->settings ? nvr_settings_ddns_list(c->settings, rows, 8) : 0;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "DDNSInfo");
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "Domain", rows[i].domain);
        cJSON_AddBoolToObject(e, "Enable", rows[i].enable);
        cJSON_AddStringToObject(e, "Password", rows[i].password);
        cJSON_AddStringToObject(e, "HostName", rows[i].hostname);
        cJSON_AddStringToObject(e, "DDNSKey", rows[i].ddns_key);
        cJSON_AddStringToObject(e, "UserName", rows[i].username);
        cJSON_AddItemToArray(arr, e);
    }
    return nvr_resp_content(o);
}

char *cmd_GUI_setDDNS(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *opt = a ? cJSON_GetObjectItem(a, "DDNSOpt") : NULL;
    if (!cJSON_IsArray(opt)) return nvr_resp_err("invalid_param");
    nvr_ddns_row_t rows[8]; int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, opt) {
        if (n >= 8 || !cJSON_IsObject(it)) continue;
        nvr_ddns_row_t *r = &rows[n++];
        r->idx = n;
        r->enable = nvr_jbool(it, "Enable", 0);
        snprintf(r->domain, sizeof(r->domain), "%s", nvr_jstr(it, "Domain", ""));
        snprintf(r->password, sizeof(r->password), "%s", nvr_jstr(it, "Password", ""));
        snprintf(r->hostname, sizeof(r->hostname), "%s", nvr_jstr(it, "HostName", ""));
        snprintf(r->ddns_key, sizeof(r->ddns_key), "%s", nvr_jstr(it, "DDNSKey", ""));
        snprintf(r->username, sizeof(r->username), "%s", nvr_jstr(it, "UserName", ""));
    }
    if (c->settings && nvr_settings_ddns_replace(c->settings, rows, n) != 0)
        return nvr_resp_err("persist_failed");
    return nvr_resp_ok();
}

char *cmd_GUI_getUPnP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    (void)a;
    nvr_upnp_cfg_t u;
    if (!c->settings || nvr_net_upnp_fill(c->settings, &u) != 0)
        return nvr_resp_err("no_settings");
    cJSON *o = cJSON_CreateObject();
    cJSON *info = cJSON_CreateObject();
    cJSON_AddBoolToObject(info, "Enable", u.enable);
    cJSON_AddNumberToObject(info, "HTTPPort", u.http_port);
    cJSON_AddNumberToObject(info, "TCPPort", u.tcp_port);
    cJSON_AddItemToObject(o, "UPnPInfo", info);
    (void)u.running;
    return nvr_resp_content(o);
}

char *cmd_GUI_setUPnP(cJSON *a, const nvr_cmd_ctx_t *c)
{
    cJSON *en = a ? cJSON_GetObjectItem(a, "UPnPEnable") : NULL;
    if (!cJSON_IsObject(en) || !c->settings) return nvr_resp_err("invalid_param");
    if (nvr_jhas(en, "Enable"))
        nvr_settings_set_int(c->settings, "network.upnp.enable", nvr_jbool(en, "Enable", 1));
    if (nvr_net_upnp_apply(c->settings) != 0)
        return nvr_resp_err("apply_failed");
    return nvr_resp_ok();
}

char *cmd_GUI_getPoE(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int channel = nvr_jint(a, "channel", 0);
    if (channel < 1 || channel > NVR_POE_PORTS) return nvr_resp_err("invalid_param");
    int enable = 0, power = 0;
    if (!c->settings || nvr_net_poe_fill(c->cm, c->settings, channel, &enable, &power) != 0)
        return nvr_resp_err("no_settings");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "Enable", enable);
    cJSON_AddNumberToObject(o, "PowerUsed", power);
    return nvr_resp_content(o);
}

char *cmd_GUI_setPoE(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int channel = nvr_jint(a, "channel", 0);
    if (channel < 1 || channel > NVR_POE_PORTS || !nvr_jhas(a, "Enable"))
        return nvr_resp_err("invalid_param");
    if (!c->settings || nvr_net_poe_apply(c->settings, channel, nvr_jbool(a, "Enable", 0)) != 0)
        return nvr_resp_err("apply_failed");
    return nvr_resp_ok();
}

char *cmd_GUI_testEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c)
{
    if (!a || !cJSON_GetObjectItem(a, "testEmailAlert"))
        return nvr_resp_err("invalid_param");
    nvr_email_cfg_t e;
    email_defaults(&e);
    if (c->settings) nvr_settings_email_get(c->settings, &e);
    if (nvr_net_email_test(&e, e.receiver[0]) != 0)
        return nvr_resp_err("smtp_failed");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "rspCode", 200);
    return nvr_resp_content(o);
}
