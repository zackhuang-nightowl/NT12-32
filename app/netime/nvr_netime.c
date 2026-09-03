/***************************************************************************************
 *  nvr_netime.c — 网络与时间应用。见 nvr_netime.h。
 *  网络：读 Linux 实时状态(getifaddrs/sysfs/proc/resolv.conf);写用 na51090 BusyBox
 *        (ifconfig/route/vconfig/udhcpc/udhcpd),与 SDK S10_Net + default.script 一致。
 ***************************************************************************************/
#include "nvr_netime.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "nvr_onvif.h"      /* nvr_onvif_set_time_now:给相机 ONVIF 授时 */
#include "nvr_channel.h"
#include "nvr_identity.h"   /* nvr_identity_get_model:eth0 DHCP 主机名=设备 model */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

static int g_synced = 0;
static volatile int g_ntp_busy = 0;   /* 一次只跑一个 ntpd 线程,防堆积 */
static long g_tz_gmtoff = -999999;    /* 上次 tick 本地偏移(秒),检测夏令自动切换 */
static int  g_tz_isdst = -1;

static int run(const char *fmt, ...)
{
    char cmd[512];
    va_list ap; va_start(ap, fmt); vsnprintf(cmd, sizeof(cmd), fmt, ap); va_end(ap);
    NVR_LOGI("net", "$ %s", cmd);
    return system(cmd);
}

#define NVR_ETH0 "eth0"
#define NVR_ETH1 "eth1"
#define NVR_UDHCPC_SCRIPT "/dvr/bin/nvr_udhcpc.script"
/* eth0 上次成功 DHCP 的 IP(持久化在 /SYS ubifs;default.script 的 bound 钩子写)。
 * 下次开机 udhcpc 用 -r 请求它 → 默认复用旧 IP;被占用(ACD)才换。 */
#define NVR_ETH0_LASTIP   "/SYS/dhcp_eth0.ip"
/* NVR 配置的 DNS(apply_dns 写);udhcpc bound/renew 后由 nvr_udhcpc.script 覆盖 resolv.conf */
#define NVR_DNS_FILE      "/SYS/nvr_dns.conf"

static int is_dhcp_type(const char *network_type)
{
    return network_type && strcasecmp(network_type, "DHCP") == 0;
}

static int read_iface_oper_up(const char *ifname);

/* /proc 扫描:eth0 上是否有 udhcpc 在跑(实时 DHCP 判定) */
static int eth0_udhcpc_running(void)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        char path[64], buf[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0) continue;
        buf[n] = 0;
        for (size_t i = 0; i + 1 < n; i++)
            if (buf[i] == 0) buf[i] = ' ';
        if (strstr(buf, "udhcpc") && strstr(buf, NVR_ETH0)) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

static void flush_default_route(const char *ifname)
{
    run("while route del default gw 0.0.0.0 dev %s 2>/dev/null; do :; done", ifname);
}

static int proc_has_process(const char *name)
{
    DIR *d = opendir("/proc");
    if (!d || !name || !name[0]) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        char path[64], buf[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0) continue;
        buf[n] = 0;
        for (size_t i = 0; i + 1 < n; i++)
            if (buf[i] == 0) buf[i] = ' ';
        if (strstr(buf, name)) { closedir(d); return 1; }
    }
    closedir(d);
    return 0;
}

static int read_iface_mac(const char *ifname, char *mac, int cap)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname ? ifname : NVR_ETH0);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(mac, cap, f)) { fclose(f); return -1; }
    fclose(f);
    size_t n = strlen(mac);
    if (n && mac[n - 1] == '\n') mac[n - 1] = 0;
    return mac[0] ? 0 : -1;
}

static int read_iface_ipv4(const char *ifname, char *ip, int ip_cap, char *mask, int mask_cap)
{
    struct ifaddrs *ifa = NULL, *it;
    if (ip) ip[0] = 0;
    if (mask) mask[0] = 0;
    if (getifaddrs(&ifa) != 0) return -1;
    for (it = ifa; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(it->ifa_name, ifname ? ifname : NVR_ETH0) != 0) continue;
        char buf[64];
        void *addr = &((struct sockaddr_in *)it->ifa_addr)->sin_addr;
        inet_ntop(AF_INET, addr, buf, sizeof(buf));
        if (ip && ip_cap > 0) snprintf(ip, (size_t)ip_cap, "%s", buf);
        if (it->ifa_netmask && mask && mask_cap > 0) {
            addr = &((struct sockaddr_in *)it->ifa_netmask)->sin_addr;
            inet_ntop(AF_INET, addr, buf, sizeof(buf));
            snprintf(mask, (size_t)mask_cap, "%s", buf);
        }
        freeifaddrs(ifa);
        return 0;
    }
    freeifaddrs(ifa);
    return -1;
}

static int read_default_gw(const char *ifname, char *gw, int cap)
{
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    while (fgets(line, sizeof(line), f)) {
        char iface[32], dest[16], gwh[16];
        if (sscanf(line, "%31s %15s %15s", iface, dest, gwh) < 3)
            continue;
        if (strcmp(dest, "00000000") != 0 || strcmp(gwh, "00000000") == 0)
            continue;
        if (ifname && ifname[0] && strcmp(iface, ifname) != 0)
            continue;
        /* /proc/net/route 网关为 hex __be32；直接赋 s_addr + inet_ntop(与 SDK util.cpp 一致) */
        struct in_addr addr;
        addr.s_addr = (in_addr_t)strtoul(gwh, NULL, 16);
        if (!inet_ntop(AF_INET, &addr, gw, (socklen_t)cap)) {
            fclose(f);
            return -1;
        }
        fclose(f);
        return 0;
    }
    fclose(f);
    return -1;
}

static void write_resolv_conf(const char *path, const char *dns1, const char *dns2)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    if (dns1 && dns1[0]) fprintf(f, "nameserver %s\n", dns1);
    if (dns2 && dns2[0]) fprintf(f, "nameserver %s\n", dns2);
    fclose(f);
}

static void apply_dns(const char *dns1, const char *dns2)
{
    write_resolv_conf("/etc/resolv.conf", dns1, dns2);
    write_resolv_conf(NVR_DNS_FILE, dns1, dns2);
}

static void local_link_from_eth0_kv(nvr_settings_t *s, nvr_local_link_t *lk)
{
    int dhcp = nvr_settings_get_int(s, "network.eth0.dhcp", 1);
    snprintf(lk->network_type, sizeof(lk->network_type), "%s", dhcp ? "DHCP" : "Static");
    nvr_settings_get_str(s, "network.eth0.ip",   lk->ip,          sizeof(lk->ip),          NVR_DEF_ETH0_IP);
    nvr_settings_get_str(s, "network.eth0.mask", lk->subnet_mask, sizeof(lk->subnet_mask), NVR_DEF_ETH0_MASK);
    nvr_settings_get_str(s, "network.eth0.gw",   lk->gateway,     sizeof(lk->gateway),     "");
    if (!lk->dns1[0]) snprintf(lk->dns1, sizeof(lk->dns1), "8.8.8.8");
    if (!lk->dns2[0]) snprintf(lk->dns2, sizeof(lk->dns2), "8.8.4.4");
}

static void sync_local_link_to_eth0_kv(nvr_settings_t *s, const nvr_local_link_t *lk)
{
    int dhcp = is_dhcp_type(lk->network_type);
    nvr_settings_set_int(s, "network.eth0.dhcp", dhcp);
    if (!dhcp) {
        nvr_settings_set_str(s, "network.eth0.ip",   lk->ip);
        nvr_settings_set_str(s, "network.eth0.mask", lk->subnet_mask);
        nvr_settings_set_str(s, "network.eth0.gw",   lk->gateway);
    }
}

static int read_iface_link_mbps(const char *ifname)
{
    char path[64], buf[16];
    if (!ifname || !ifname[0]) return -1;
    snprintf(path, sizeof(path), "/sys/class/net/%s/speed", ifname);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    fclose(f);
    int mbps = atoi(buf);
    return mbps > 0 ? mbps : -1;
}

int nvr_net_eth0_link_mbps(void)
{
    return read_iface_link_mbps(NVR_ETH0);
}

int nvr_net_lan_bandwidth_mbps(int *total_mbps, int *max_rx_mbps)
{
    int mbps = read_iface_link_mbps(NVR_ETH0);
    if (mbps <= 0) return -1;
    if (total_mbps) *total_mbps = mbps;
    if (max_rx_mbps) *max_rx_mbps = mbps;
    return 0;
}

static int is_wifi_iface(const char *name)
{
    return name && (strncmp(name, "wlan", 4) == 0 || strncmp(name, "wl", 2) == 0);
}

static int is_eth_iface(const char *name)
{
    return name && strncmp(name, "eth", 3) == 0;
}

int nvr_net_wan_fill(nvr_wan_if_info_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int has_eth = 0, has_wifi = 0, eth_up = 0, wifi_up = 0;
    DIR *d = opendir("/sys/class/net");
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (!name[0] || name[0] == '.') continue;
        if (strcmp(name, "lo") == 0) continue;

        int up = read_iface_oper_up(name);
        if (is_wifi_iface(name)) {
            has_wifi = 1;
            if (up) wifi_up = 1;
        } else if (is_eth_iface(name)) {
            has_eth = 1;
            if (strcmp(name, NVR_ETH0) == 0 && up) eth_up = 1;
        }
    }
    closedir(d);

    if (!has_eth && !has_wifi) {
        has_eth = 1;
        snprintf(out->value, sizeof(out->value), "eth");
        out->list[out->list_n++] = "eth";
        return 0;
    }

    if (has_eth) out->list[out->list_n++] = "eth";
    if (has_wifi) out->list[out->list_n++] = "wifi";

    if (eth_up)
        snprintf(out->value, sizeof(out->value), "eth");
    else if (wifi_up)
        snprintf(out->value, sizeof(out->value), "wifi");
    else
        snprintf(out->value, sizeof(out->value), has_eth ? "eth" : "wifi");

    if (eth_up) out->connected[out->conn_n++] = "eth";
    if (wifi_up) out->connected[out->conn_n++] = "wifi";
    return 0;
}

int nvr_net_local_link_fill(nvr_settings_t *s, nvr_local_link_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (nvr_settings_local_link_get(s, out) != 0)
        local_link_from_eth0_kv(s, out);

    /* GET 以 Linux 实时状态为准(设置库仅作缺省/持久化) */
    read_iface_mac(NVR_ETH0, out->mac, sizeof(out->mac));

    char lip[64], lmask[64], lgw[64];
    if (read_iface_ipv4(NVR_ETH0, lip, sizeof(lip), lmask, sizeof(lmask)) == 0) {
        if (lip[0])  snprintf(out->ip, sizeof(out->ip), "%s", lip);
        if (lmask[0]) snprintf(out->subnet_mask, sizeof(out->subnet_mask), "%s", lmask);
    }
    if (read_default_gw(NVR_ETH0, lgw, sizeof(lgw)) == 0 && lgw[0])
        snprintf(out->gateway, sizeof(out->gateway), "%s", lgw);

    /* DNS 以 local_link 持久化配置为准(udhcpc 会改 resolv.conf,GET 不应被覆盖) */
    if (!out->dns1[0]) snprintf(out->dns1, sizeof(out->dns1), "8.8.8.8");
    if (!out->dns2[0]) snprintf(out->dns2, sizeof(out->dns2), "8.8.4.4");

    if (eth0_udhcpc_running())
        snprintf(out->network_type, sizeof(out->network_type), "DHCP");
    else if (!out->network_type[0])
        snprintf(out->network_type, sizeof(out->network_type), "Static");

    if (!out->subnet_mask[0]) snprintf(out->subnet_mask, sizeof(out->subnet_mask), "%s", NVR_DEF_ETH0_MASK);
    return 0;
}

int nvr_net_apply_eth0(nvr_settings_t *s)
{
    if (!s) return -1;
    nvr_local_link_t lk;
    nvr_net_local_link_fill(s, &lk);

    /* 先写 DNS(含 /SYS/nvr_dns.conf),DHCP bound 后 udhcpc 脚本会再覆盖 resolv.conf */
    apply_dns(lk.dns1, lk.dns2);

    if (is_dhcp_type(lk.network_type)) {
        run("killall udhcpc 2>/dev/null");
        run("ifconfig %s up 2>/dev/null", NVR_ETH0);
        /* -r <上次IP>:默认复用旧 IP(default.script bound 时写 NVR_ETH0_LASTIP);
         *   被占用则 default.script 的 RFC5227 ACD 退让,udhcpc 再申请新 IP。
         * -x hostname:<model>:DHCP Option 12 上报设备 model(权威 /User/OWLModel),
         *   路由器/上级设备清单可按型号识别本机。 */
        char ropt[80] = "", hopt[112] = "";
        {
            char lastip[64] = "";
            FILE *lf = fopen(NVR_ETH0_LASTIP, "r");
            if (lf) {
                if (fgets(lastip, sizeof(lastip), lf))
                    lastip[strcspn(lastip, " \r\n\t")] = 0;
                fclose(lf);
            }
            if (lastip[0])
                snprintf(ropt, sizeof(ropt), "-r %s ", lastip);
            char model[48] = "";
            nvr_identity_get_model(model, sizeof(model));
            if (model[0]) {
                snprintf(hopt, sizeof(hopt), "-x hostname:%s ", model);
                run("hostname %s 2>/dev/null", model);   /* 本机主机名也设为 model */
            }
        }
        run("udhcpc -i %s -b -q %s%s-s %s 2>/dev/null &",
            NVR_ETH0, ropt, hopt, NVR_UDHCPC_SCRIPT);
        NVR_LOGI("net", "%s = DHCP (udhcpc %s%s, dns=%s/%s)", NVR_ETH0,
                 ropt[0] ? ropt : "", hopt[0] ? hopt : "-x hostname:? ",
                 lk.dns1, lk.dns2);
    } else {
        run("killall udhcpc 2>/dev/null");
        run("ifconfig %s %s netmask %s up 2>/dev/null", NVR_ETH0, lk.ip, lk.subnet_mask);
        flush_default_route(NVR_ETH0);
        if (lk.gateway[0])
            run("route add default gw %s dev %s 2>/dev/null", lk.gateway, NVR_ETH0);
        NVR_LOGI("net", "%s = 静态 %s/%s gw=%s dns=%s/%s", NVR_ETH0,
                 lk.ip, lk.subnet_mask, lk.gateway, lk.dns1, lk.dns2);
    }
    return 0;
}

int nvr_net_local_link_apply(nvr_settings_t *s, const nvr_local_link_t *in)
{
    if (!s || !in) return -1;
    if (!is_dhcp_type(in->network_type)) {
        if (!in->ip[0] || !in->subnet_mask[0])
            return -1;
    }
    nvr_local_link_t row = *in;
    if (!row.mac[0]) read_iface_mac(NVR_ETH0, row.mac, sizeof(row.mac));
    if (nvr_settings_local_link_set(s, &row) != 0) return -1;
    sync_local_link_to_eth0_kv(s, &row);
    return nvr_net_apply_eth0(s);
}

/* ---------------- 网络 ---------------- */
/* ★ PoE 板硬件规格(端口 PVID 示意图 + 真机确认):物理口 P 的 PVID 与相邻口**对调**——
 *   口1→VLAN2002、口2→VLAN2001、口3→VLAN2004…口15→VLAN2016、口16→VLAN2015。
 *   即 PVID = vlan_base + swap(P),swap 对调相邻奇偶(1↔2,3↔4,…,15↔16)。
 *   故建 VLAN 接口/查口 VLAN 都走此换算(否则口P相机会错位到相邻口通道)。
 *   段(198.18.P)仍=口号 P:eth1.(base+swap(P)) 配 198.18.P.100,让口P相机拿段P。 */
#define POE_PVID_OFF(p)  (((p) & 1) ? (p) + 1 : (p) - 1)   /* 口→PVID 偏移:1↔2,3↔4,…,15↔16 */

int nvr_net_apply(nvr_settings_t *s)
{
    if (!s) return -1;

    /* eth0：local_link / network.eth0.* → 应用 */
    if (nvr_net_apply_eth0(s) != 0)
        NVR_LOGW("net", "eth0 应用失败,继续 PoE 口配置");

    /* eth1：PoE 汇聚口 —— 每 PoE 口一个 VLAN(2001..2016)，网段 198.18.<口>.0/24。
     * ★ VLAN DHCP 服务端已统一到**基座 ISC dhcpd**(/etc/init.d/service.dhcpd +
     *   /etc/dhcpd_vlan.conf,init.d 先于 nvr_app 起,持久租约落 /SYS)。此处不再另起
     *   busybox udhcpd —— 否则两个 DHCP server 抢 eth1.<vid> UDP:67(端口占用竞态、
     *   规则不一致)。本函数只负责把 VLAN 接口与 NVR 侧 .100 网关地址拉起(与基座
     *   S30eth1vlan 幂等重合),供 ISC dhcpd 绑定与发现取流用源地址。 */
    int poe = nvr_settings_get_int(s, "system.poe_ports", 16);
    int vlan_base = nvr_settings_get_int(s, "network.eth1.vlan_base", NVR_DEF_VLAN_BASE);
    run("ifconfig %s up 2>/dev/null", NVR_ETH1);
    for (int p = 1; p <= poe; p++) {
        char pkey[48];
        snprintf(pkey, sizeof(pkey), "network.poe.port.%d.enable", p);
        if (!nvr_settings_get_int(s, pkey, 1)) continue;

        int vid = vlan_base + POE_PVID_OFF(p);   /* 口p→PVID(相邻奇偶对调,硬件规格) */
        run("vconfig add %s %d 2>/dev/null", NVR_ETH1, vid);
        run("ifconfig eth1.%d 198.18.%d.100 netmask 255.255.255.0 up 2>/dev/null", vid, p);
    }
    NVR_LOGI("net", "eth1 PoE 汇聚: %d 个 VLAN(%d..%d), NVR 侧 198.18.<口>.100；DHCP=基座 ISC dhcpd",
             poe, vlan_base + 1, vlan_base + poe);
    nvr_net_upnp_apply(s);
    return 0;
}

/* ---------------- UPnP / PoE / SMTP ---------------- */

static int read_iface_oper_up(const char *ifname)
{
    char path[128], st[16];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifname);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(st, sizeof(st), f)) { fclose(f); return 0; }
    fclose(f);
    return (strncmp(st, "up", 2) == 0 || strncmp(st, "unknown", 7) == 0);
}

int nvr_net_upnp_fill(nvr_settings_t *s, nvr_upnp_cfg_t *out)
{
    if (!s || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->enable    = nvr_settings_get_int(s, "network.upnp.enable", 1);
    out->http_port = nvr_settings_get_int(s, "network.upnp.http_port",
                    nvr_settings_get_int(s, "network.port.http", 80));
    out->tcp_port  = nvr_settings_get_int(s, "network.upnp.tcp_port",
                    nvr_settings_get_int(s, "network.port.tcp", NVR_DEF_NOP_PORT));
    out->running   = proc_has_process("miniupnpd") || proc_has_process("upnpd");
    return 0;
}

int nvr_net_upnp_apply(nvr_settings_t *s)
{
    if (!s) return -1;
    nvr_upnp_cfg_t u;
    nvr_net_upnp_fill(s, &u);
    run("killall miniupnpd 2>/dev/null; killall upnpd 2>/dev/null");
    if (!u.enable) {
        NVR_LOGI("net", "UPnP 已关闭");
        return 0;
    }
    FILE *f = fopen("/tmp/nvr_miniupnpd.conf", "w");
    if (!f) return -1;
    fprintf(f,
        "ext_ifname=%s\n"
        "listening_ip=\n"
        "port=5000\n"
        "enable_upnp=yes\n"
        "secure_mode=no\n"
        "presentation_url=http://%s:%d/\n",
        NVR_ETH0, NVR_ETH0, u.http_port);
    fclose(f);
    run("miniupnpd -f /tmp/nvr_miniupnpd.conf -d 2>/dev/null || "
        "upnpd -f /tmp/nvr_miniupnpd.conf 2>/dev/null &");
    NVR_LOGI("net", "UPnP 已启动 http=%d tcp=%d", u.http_port, u.tcp_port);
    return 0;
}

static int poe_vlan_id(nvr_settings_t *s, int channel)
{
    int base = nvr_settings_get_int(s, "network.eth1.vlan_base", NVR_DEF_VLAN_BASE);
    return base + POE_PVID_OFF(channel);   /* 口→PVID 相邻对调(硬件规格) */
}

int nvr_net_poe_fill(nvr_chan_mgr_t *cm, nvr_settings_t *s, int channel,
                     int *enable, int *power_used)
{
    if (!s || channel < 1 || channel > NVR_POE_PORTS) return -1;
    char key[48];
    snprintf(key, sizeof(key), "network.poe.port.%d.enable", channel);
    int cfg_en = nvr_settings_get_int(s, key, 1);

    char ifname[32];
    snprintf(ifname, sizeof(ifname), "eth1.%d", poe_vlan_id(s, channel));
    int link_up = read_iface_oper_up(ifname);

    if (enable) *enable = cfg_en && link_up;
    if (power_used) {
        *power_used = 0;
        if (cm && cfg_en && link_up) {
            nvr_chan_status_t st = nvr_chan_status(cm, channel - 1);
            if (st == NVR_CHAN_ONLINE || st == NVR_CHAN_CONNECTING)
                *power_used = 5;
        }
    }
    return 0;
}

int nvr_net_poe_apply(nvr_settings_t *s, int channel, int enable)
{
    if (!s || channel < 1 || channel > NVR_POE_PORTS) return -1;
    char key[48];
    snprintf(key, sizeof(key), "network.poe.port.%d.enable", channel);
    nvr_settings_set_int(s, key, enable ? 1 : 0);
    int vid = poe_vlan_id(s, channel);
    if (enable)
        run("vconfig add %s %d 2>/dev/null; ifconfig eth1.%d 198.18.%d.100 netmask 255.255.255.0 up 2>/dev/null",
            NVR_ETH1, vid, vid, channel);
    else
        run("ifconfig eth1.%d down 2>/dev/null", vid);
    NVR_LOGI("net", "PoE 口%d %s (eth1.%d)", channel, enable ? "启用" : "关闭", vid);
    return 0;
}

static int smtp_read_reply(int fd, int expect, char *err, int err_cap)
{
    char line[512];
    int code = 0;
    for (;;) {
        ssize_t n = 0;
        size_t pos = 0;
        while (pos + 1 < sizeof(line)) {
            ssize_t r = recv(fd, line + pos, 1, 0);
            if (r <= 0) return -1;
            if (line[pos] == '\n') { line[pos + 1] = 0; n = (ssize_t)(pos + 1); break; }
            pos++;
        }
        if (n <= 0) return -1;
        if (code == 0) code = atoi(line);
        if (strlen(line) < 4 || line[3] != '-') break;
    }
    if (expect && code != expect) {
        if (err && err_cap > 0) snprintf(err, (size_t)err_cap, "smtp %d", code);
        return -1;
    }
    return code;
}

static int smtp_send_cmd(int fd, const char *cmd, int expect, char *err, int err_cap)
{
    if (cmd && send(fd, cmd, strlen(cmd), 0) < 0) return -1;
    return smtp_read_reply(fd, expect, err, err_cap);
}

static void smtp_b64_encode(const char *in, char *out, int cap)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, o = 0;
    unsigned char a, b, c;
    while (in[i] && o + 4 < cap) {
        a = (unsigned char)in[i++];
        b = in[i] ? (unsigned char)in[i++] : 0;
        c = in[i] ? (unsigned char)in[i++] : 0;
        out[o++] = tbl[a >> 2];
        out[o++] = tbl[((a & 3) << 4) | (b >> 4)];
        out[o++] = in[i - 1] ? tbl[((b & 15) << 2) | (c >> 6)] : '=';
        out[o++] = in[i] ? tbl[c & 63] : '=';
    }
    if (o + 2 < cap) { out[o++] = '\r'; out[o++] = '\n'; out[o] = 0; }
}

int nvr_net_email_test(const nvr_email_cfg_t *cfg, const char *receiver)
{
    if (!cfg || !cfg->smtp_server[0]) return -1;
    if (cfg->use_ssl && cfg->smtp_port == 465) {
        NVR_LOGW("net", "SMTP SSL/465 暂未实现,请用 587/25 测试");
        return -1;
    }
    const char *rcpt = receiver && receiver[0] && strcmp(receiver, "none") != 0
                     ? receiver : cfg->receiver[0];
    if (!rcpt || !rcpt[0] || strcmp(rcpt, "none") == 0) return -1;

    char port[16];
    snprintf(port, sizeof(port), "%d", cfg->smtp_port > 0 ? cfg->smtp_port : 25);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(cfg->smtp_server, port, &hints, &res) != 0 || !res) return -1;

    int fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = 15, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    char err[64] = {0};
    if (smtp_read_reply(fd, 220, err, sizeof(err)) < 0) { close(fd); return -1; }
    if (smtp_send_cmd(fd, "EHLO nvr.local\r\n", 250, err, sizeof(err)) < 0) { close(fd); return -1; }

    if (cfg->username[0]) {
        char user[256], pass[256], cmd[512];
        smtp_b64_encode(cfg->username, user, sizeof(user));
        smtp_b64_encode(cfg->password, pass, sizeof(pass));
        if (smtp_send_cmd(fd, "AUTH LOGIN\r\n", 334, err, sizeof(err)) < 0) goto smtp_fail;
        snprintf(cmd, sizeof(cmd), "%s", user);
        if (smtp_send_cmd(fd, cmd, 334, err, sizeof(err)) < 0) goto smtp_fail;
        snprintf(cmd, sizeof(cmd), "%s", pass);
        if (smtp_send_cmd(fd, cmd, 235, err, sizeof(err)) < 0) goto smtp_fail;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>\r\n",
             cfg->sender[0] ? cfg->sender : cfg->username);
    if (smtp_send_cmd(fd, cmd, 250, err, sizeof(err)) < 0) goto smtp_fail;
    snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>\r\n", rcpt);
    if (smtp_send_cmd(fd, cmd, 250, err, sizeof(err)) < 0) goto smtp_fail;
    if (smtp_send_cmd(fd, "DATA\r\n", 354, err, sizeof(err)) < 0) goto smtp_fail;
    snprintf(cmd, sizeof(cmd),
             "Subject: %s\r\nFrom: <%s>\r\nTo: <%s>\r\n\r\nNVR email test.\r\n.\r\n",
             cfg->title[0] ? cfg->title : "NVR Test", cfg->sender, rcpt);
    if (smtp_send_cmd(fd, cmd, 250, err, sizeof(err)) < 0) goto smtp_fail;
    smtp_send_cmd(fd, "QUIT\r\n", 221, NULL, 0);
    close(fd);
    NVR_LOGI("net", "SMTP 测试邮件已发送至 %s", rcpt);
    return 0;
smtp_fail:
    NVR_LOGW("net", "SMTP 失败: %s", err[0] ? err : "unknown");
    close(fd);
    return -1;
}

/* ---------------- 时间（UTC 内部时钟 + 时区显示 + NTP） ----------------
 *
 * 时区模型(全系统一致):内部时钟恒 UTC(epoch);显示/排程按本地时区换算。
 * 关键:GUI(nightowl-lvgl)与 nvr_app 是**两个进程**,都靠 glibc 的 /etc/localtime
 * (TZ 未设时的默认 tzfile)取本地时区。故 NVR 负责把 /etc/localtime 设对,两进程即对齐。
 *
 * 设备现状/坑:GUI 送的时区是 "GMT -5:00" 这种**非法 POSIX TZ 串**,旧代码 setenv("TZ",它)
 * → tzset 解析失败 → 恒 UTC(=时区不生效根因);且 /etc/localtime 悬空指到不存在的 /flash/65D。
 * 设备无 /usr/share/zoneinfo。故这里:①把 timezone/tz_dst 转成合法 POSIX 串;②自生成极简 TZif
 * 写 /flash(持久 ubifs);③/etc/localtime(RAM rootfs 可写)软链到它,每次开机/改时区重建。
 */

#define NVR_TZFILE "/flash/nvr_localtime"   /* 持久 tzfile(ubifs);/etc/localtime 软链到此 */

/* 大端写入小工具 */
static uint8_t *put32(uint8_t *p, uint32_t v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; return p+4; }
static uint8_t *put64(uint8_t *p, uint64_t v) { for (int i=7;i>=0;i--) *p++=(uint8_t)(v>>(i*8)); return p; }

/* 生成极简 TZif v2(1 个 1970 transition→type0,footer=POSIX 规则)。glibc 对"当下"用 footer 规则,
 * 固定偏移("GMT5")与含夏令("EST5EDT,...")统一走此形式。已在设备 glibc 2.30 实测验证。 */
static int write_min_tzif(const char *posix, const char *path)
{
    uint8_t buf[256], *p = buf;
    const uint8_t desig[8] = {'S','T','D',0,'D','S','T',0};   /* charcnt=8 */
    for (int v = 0; v < 2; v++) {                 /* v0=V1 块(4B 时间), v1=V2 块(8B 时间) */
        memcpy(p, "TZif", 4); p += 4;
        *p++ = '2';                               /* version */
        memset(p, 0, 15); p += 15;
        p = put32(p, 0);   /* isutcnt */
        p = put32(p, 0);   /* isstdcnt */
        p = put32(p, 0);   /* leapcnt  */
        p = put32(p, 1);   /* timecnt  = 1 */
        p = put32(p, 2);   /* typecnt  = 2 (std,dst) */
        p = put32(p, 8);   /* charcnt  = 8 */
        if (v == 0) p = put32(p, 0); else p = put64(p, 0);   /* 1 个 transition @ epoch 0 */
        *p++ = 0;                                            /* → type index 0 */
        p = put32(p, 0); *p++ = 0; *p++ = 0;                 /* ttinfo[0]: utoff=0 isdst=0 desig=0 */
        p = put32(p, 0); *p++ = 1; *p++ = 4;                 /* ttinfo[1]: utoff=0 isdst=1 desig=4 */
        memcpy(p, desig, 8); p += 8;
    }
    *p++ = '\n';
    size_t n = strlen(posix); memcpy(p, posix, n); p += n;
    *p++ = '\n';

    char tmp[128]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { NVR_LOGW("time", "写 tzfile 失败: %s", tmp); return -1; }
    fwrite(buf, 1, (size_t)(p - buf), f);
    fclose(f);
    rename(tmp, path);
    return 0;
}

/* 把 system.timezone("GMT ±H:MM") 转成 POSIX 固定偏移串(不含夏令规则)。 */
static void gmt_str_to_posix(const char *gmt_tz, char *out, int cap)
{
    const char *p = gmt_tz ? gmt_tz : "";
    if (strncmp(p, "GMT", 3) == 0) p += 3;
    while (*p == ' ') p++;
    int sign = 1;
    if      (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    int H = 0, M = 0;
    sscanf(p, "%d:%d", &H, &M);
    int posix_min = -(sign * (H * 60 + M));
    int ah = (posix_min < 0 ? -posix_min : posix_min) / 60;
    int am = (posix_min < 0 ? -posix_min : posix_min) % 60;
    snprintf(out, cap, "GMT%s%d:%02d", posix_min < 0 ? "-" : "", ah, am);
}

int nvr_tz_parse_gmt_offset_min(const char *timezone)
{
    const char *p = timezone ? timezone : "";
    if (strncmp(p, "GMT", 3) == 0) p += 3;
    while (*p == ' ') p++;
    int sign = 1;
    if      (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    else if (*p == '\0') return -1;
    int H = 0, M = 0;
    if (sscanf(p, "%d:%d", &H, &M) < 1) return -1;
    return sign * (H * 60 + M);
}

/* GUI SystemInfo 24 区中实际有夏令切换的 UTC 偏移(分钟,东为正)。 */
int nvr_tz_offset_supports_dst(int offset_min)
{
    static const int k[] = {
        -720,-660,-600,-570,-540,-510,-480,-420,-390,-360,-330,-300,-270,-240,-210,-180,-120,-60,
        0, 60, 120, 180, 210, 570, 600, 720, 780
    };
    for (size_t i = 0; i < sizeof(k)/sizeof(k[0]); i++)
        if (k[i] == offset_min) return 1;
    return 0;
}

/* 从 POSIX tz_dst 串解析标准偏移(显示分钟,东为正)。 */
static int posix_std_offset_display_min(const char *posix)
{
    const char *p = posix ? posix : "";
    while (*p && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++;
    if (!*p) return -9999;
    int sign = 1;
    if      (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    else return -9999;
    int H = 0, M = 0;
    if (sscanf(p, "%d:%d", &H, &M) < 1) return -9999;
    return -(sign * (H * 60 + M));
}

int nvr_tz_validate_set(const char *timezone, const char *tz_dst)
{
    int off;
    if (!tz_dst || !tz_dst[0]) return 0;
    if (!strchr(tz_dst, ',')) return -1;
    if (timezone && timezone[0]) {
        off = nvr_tz_parse_gmt_offset_min(timezone);
    } else {
        off = posix_std_offset_display_min(tz_dst);
    }
    if (off == -9999) return -1;
    if (!nvr_tz_offset_supports_dst(off)) return -2;
    return 0;
}

void nvr_time_build_onvif_cfg(nvr_settings_t *s, nvr_onvif_time_cfg_t *cfg)
{
    char dst[80] = {0}, tz[64];
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    if (s) {
        nvr_settings_get_str(s, "system.tz_dst", dst, sizeof(dst), "");
        nvr_settings_get_str(s, "system.timezone", tz, sizeof(tz), NVR_DEF_TIMEZONE);
    }
    if (dst[0]) {
        snprintf(cfg->tz_posix, sizeof(cfg->tz_posix), "%s", dst);
        cfg->daylight = 1;
    } else {
        char posix[96];
        if (tz[0]) gmt_str_to_posix(tz, posix, sizeof(posix));
        else {
            time_t now = time(NULL);
            struct tm lt;
            localtime_r(&now, &lt);
            long off = lt.tm_gmtoff;
            int sign = (off < 0) ? -1 : 1;
            long a = (off < 0) ? -off : off;
            int hh = (int)(a / 3600), mm = (int)((a % 3600) / 60);
            int posix_min = -(sign * (hh * 60 + mm));
            int ah = (posix_min < 0 ? -posix_min : posix_min) / 60;
            int am = (posix_min < 0 ? -posix_min : posix_min) % 60;
            snprintf(posix, sizeof(posix), "GMT%s%d:%02d",
                     posix_min < 0 ? "-" : "", ah, am);
        }
        snprintf(cfg->tz_posix, sizeof(cfg->tz_posix), "%s", posix);
        cfg->daylight = 0;
    }
}

/* 把 system.timezone("GMT ±H:MM")/ system.tz_dst(POSIX DST 串) 转成合法 POSIX TZ 串。
 * tz_dst 若非空按协议优先(已是 POSIX,原样用);否则由 GMT 偏移换算(注意 POSIX 符号相反:
 * 显示 UTC-5 → POSIX 偏移 +5 → "GMT5";显示 UTC+8 → "GMT-8")。 */
static void build_posix_tz(nvr_settings_t *s, char *out, int cap)
{
    char dst[80] = {0};
    nvr_settings_get_str(s, "system.tz_dst", dst, sizeof(dst), "");
    if (dst[0]) { snprintf(out, cap, "%s", dst); return; }

    char tz[64]; nvr_settings_get_str(s, "system.timezone", tz, sizeof(tz), NVR_DEF_TIMEZONE);
    gmt_str_to_posix(tz, out, cap);
}

/* 安装时区(不含 NTP):构造 POSIX → 生成 TZif → 软链 /etc/localtime → 本进程 tzset。
 * 供开机(nvr_time_apply)与 setTimezone 命令即时调用。 */
int nvr_tz_install(nvr_settings_t *s)
{
    if (!s) return -1;
    char posix[96]; build_posix_tz(s, posix, sizeof(posix));
    if (write_min_tzif(posix, NVR_TZFILE) != 0) return -1;
    run("ln -sf %s /etc/localtime", NVR_TZFILE);      /* /etc 为 RAM rootfs 可写;开机重建 */
    unsetenv("TZ"); tzset();                           /* 本进程改读 /etc/localtime */
    char disp[64], dst[80];
    nvr_settings_get_str(s, "system.timezone", disp, sizeof(disp), NVR_DEF_TIMEZONE);
    nvr_settings_get_str(s, "system.tz_dst",   dst,  sizeof(dst),  "");
    time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
    /* 日志显示**人类时区串**(用户设的),POSIX 仅附注(其符号与习惯相反:POSIX 'GMT8'=UTC-8)。 */
    NVR_LOGI("time", "时区安装: 时区='%s'%s%s → 本地=%04d-%02d-%02d %02d:%02d:%02d (内部 POSIX='%s')",
             disp, dst[0] ? " dst=" : "", dst[0] ? dst : "",
             lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, posix);
    return 0;
}

/* ---- 相机 ONVIF 授时:把 NVR 当前时间下发所有已添加相机(改时区/改时间时触发) ---- */
#define NVR_TZ_CAM_CAP 64   /* 相机快照上限(=最大通道数) */
typedef struct {
    nvr_camera_row_t row;
    nvr_onvif_time_cfg_t tz_cfg;
} cam_push_one_ctx_t;

typedef struct {
    int n;
    nvr_camera_row_t rows[NVR_TZ_CAM_CAP];
    nvr_onvif_time_cfg_t tz_cfg;
} cam_push_ctx_t;

static int cam_push_one_try(const nvr_camera_row_t *r, const nvr_onvif_time_cfg_t *tz)
{
    int port, attempt, ok = 0;
    if (!r || !r->ip[0] || !tz) return 0;
    port = r->onvif_port > 0 ? r->onvif_port : 80;
    for (attempt = 1; attempt <= 3; attempt++) {
        if (nvr_onvif_set_time_now(r->ip, port, r->username, r->password, tz) == 0) {
            ok = 1;
            break;
        }
        NVR_LOGW("time", "相机授时失败(第%d/3次) ip=%s port=%d", attempt, r->ip, port);
        if (attempt < 3) sleep(2);
    }
    if (ok)
        NVR_LOGI("time", "相机授时 OK ip=%s port=%d (dst=%s tz=%s)",
                 r->ip, port, tz->daylight ? "on" : "off", tz->tz_posix);
    else
        NVR_LOGW("time", "相机授时最终失败 ip=%s port=%d", r->ip, port);
    return ok;
}

static void *cam_push_one_thread(void *arg)
{
    cam_push_one_ctx_t *c = (cam_push_one_ctx_t *)arg;
    if (c) {
        (void)cam_push_one_try(&c->row, &c->tz_cfg);
        free(c);
    }
    return NULL;
}

static int cam_push_one_async(nvr_settings_t *s, const nvr_camera_row_t *row)
{
    cam_push_one_ctx_t *ctx;
    pthread_t th;
    pthread_attr_t at;

    if (!s || !row || !row->ip[0]) return -1;
    ctx = (cam_push_one_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return -1;
    ctx->row = *row;
    nvr_time_build_onvif_cfg(s, &ctx->tz_cfg);
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, cam_push_one_thread, ctx) != 0) {
        pthread_attr_destroy(&at);
        free(ctx);
        return -1;
    }
    pthread_attr_destroy(&at);
    NVR_LOGI("time", "相机 ONVIF 授时: 后台下发 ip=%s", row->ip);
    return 0;
}

int nvr_time_push_device(nvr_settings_t *s, const char *ip, int port,
                         const char *user, const char *pass)
{
    nvr_camera_row_t row;
    if (!s || !ip || !ip[0]) return -1;
    memset(&row, 0, sizeof(row));
    snprintf(row.ip, sizeof(row.ip), "%s", ip);
    row.onvif_port = port > 0 ? port : 80;
    row.enabled = 1;
    if (user) snprintf(row.username, sizeof(row.username), "%s", user);
    if (pass) snprintf(row.password, sizeof(row.password), "%s", pass);
    return cam_push_one_async(s, &row);
}

static void *cam_push_thread(void *arg)
{
    cam_push_ctx_t *c = (cam_push_ctx_t *)arg;
    int ok = 0, attempted = 0, i;
    if (!c) return NULL;
    for (i = 0; i < c->n; i++) {
        nvr_camera_row_t *r = &c->rows[i];
        if (!r->enabled || !r->ip[0]) continue;
        attempted++;
        if (cam_push_one_try(r, &c->tz_cfg)) ok++;
    }
    NVR_LOGI("time", "相机 ONVIF 授时完成: %d/%d (dst=%s tz=%s)",
             ok, attempted, c->tz_cfg.daylight ? "on" : "off", c->tz_cfg.tz_posix);
    free(c);
    return NULL;
}

/* 后台异步:遍历相机逐台 ONVIF 授时(每台含发现,耗时,不可阻塞命令线程)。
 * 在调用线程快照相机表(避免 bg 线程并发访问 sqlite),再交给 detached 线程执行。 */
int nvr_time_push_cameras(nvr_settings_t *s)
{
    if (!s) return -1;
    cam_push_ctx_t *ctx = (cam_push_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return -1;
    ctx->n = nvr_settings_camera_list(s, ctx->rows, NVR_TZ_CAM_CAP);
    if (ctx->n <= 0) { free(ctx); NVR_LOGI("time", "无已添加相机,跳过授时"); return 0; }
    nvr_time_build_onvif_cfg(s, &ctx->tz_cfg);
    pthread_t th;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, cam_push_thread, ctx) != 0) { pthread_attr_destroy(&at); free(ctx); return -1; }
    pthread_attr_destroy(&at);
    NVR_LOGI("time", "相机 ONVIF 授时: 后台下发 %d 台", ctx->n);
    return 0;
}

void nvr_time_notify_changed(nvr_settings_t *s, const char *reason)
{
    time_t now;
    int force = 0;
    if (!s) return;
    if (reason && (strcmp(reason, "ntp") == 0 || strcmp(reason, "set_clock") == 0))
        force = 1;
    now = time(NULL);
    static time_t debounce_until;
    if (!force && now < debounce_until) {
        NVR_LOGI("time", "相机授时 debounce 跳过 (%s)", reason ? reason : "?");
        return;
    }
    if (!force) debounce_until = now + 5;
    NVR_LOGI("time", "时钟/时区变化 → 相机授时 (%s)", reason ? reason : "?");
    nvr_time_push_cameras(s);
}

static void tz_snapshot_update(void)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    g_tz_gmtoff = lt.tm_gmtoff;
    g_tz_isdst  = lt.tm_isdst;
}

/* ---- NTP 一次性授时(异步线程)----
 * 设备的 ntpd = **ISC ntpd 4.2.x**(非 busybox)。三个关键坑,旧代码全踩:
 *  ① 服务器是**位置参数**(命令行末尾),ISC 的 `-p` 是 **pidfile**;旧 `-p pool.ntp.org` 把服务器
 *     当 pid 文件 → 从不查任何网络服务器。
 *  ② /etc/ntp.conf 内含**本地参考时钟** `server 127.127.1.0 prefer`;`ntpd -q` 会秒同步到本地
 *     (offset 0)立即退出,永不等网络 → 恒 "time slew 0"。故用**只含网络服务器的最小 conf**
 *     (`-c` 覆盖),配 iburst 快速起。
 *  ③ ntpd `-q` 在服务器不可达时会阻塞**数分钟**才返回 → 必须放**独立 detached 线程**,绝不阻塞
 *     开机/主循环 tick。
 * `-g` 允许首次大幅 step(开机 RTC 可能差数小时);`-q` 同步完成即退;`-n` 前台。
 * 成功(最小 conf 无本地时钟,rc==0 即真同步)→ `hwclock -w` 写回 RTC(下次开机/离线保留)+ g_synced。 */
typedef struct { char ntp[128]; nvr_settings_t *settings; } ntp_sync_arg_t;

static void *ntp_sync_thread(void *arg)
{
    ntp_sync_arg_t *a = (ntp_sync_arg_t *)arg;
    if (!a) return NULL;
    FILE *f = fopen("/tmp/nvr_ntp.conf", "w");
    if (f) {
        fprintf(f, "server %s iburst\nserver %s iburst\nserver %s iburst\n",
                a->ntp, NVR_DEF_NTP2, NVR_DEF_NTP3);
        fclose(f);
    }
    int rc = system("ntpd -gq -n -c /tmp/nvr_ntp.conf >/dev/null 2>&1");
    if (rc == 0) {
        system("hwclock -w >/dev/null 2>&1");
        g_synced = 1;
        time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
        NVR_LOGI("time", "NTP 同步成功 → 本地 %04d-%02d-%02d %02d:%02d:%02d (已写 RTC)",
                 lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
        tz_snapshot_update();
        if (a->settings) nvr_time_notify_changed(a->settings, "ntp");
    } else {
        NVR_LOGW("time", "NTP 未成功(离线/服务器不可达?), 稍后重试");
    }
    g_ntp_busy = 0;
    free(a);
    return NULL;
}

/* 触发一次异步 NTP 授时。门控:自动授时开关关(system.time_sync=0=手动)/已同步/已有线程在跑 → 跳过。 */
static void ntp_sync_async(nvr_settings_t *s)
{
    ntp_sync_arg_t *a;
    if (!s || g_synced || g_ntp_busy) return;
    if (!nvr_settings_get_int(s, "system.time_sync", 1)) return;
    a = (ntp_sync_arg_t *)calloc(1, sizeof(*a));
    if (!a) return;
    a->settings = s;
    nvr_settings_get_str(s, "system.ntp", a->ntp, sizeof(a->ntp), NVR_DEF_NTP1);
    g_ntp_busy = 1;
    pthread_t th; pthread_attr_t at;
    pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, ntp_sync_thread, a) != 0) { g_ntp_busy = 0; free(a); }
    pthread_attr_destroy(&at);
}

int nvr_time_apply(nvr_settings_t *s)
{
    if (!s) return -1;
    nvr_tz_install(s);
    tz_snapshot_update();
    /* IPC 授时在通道首次出图时逐台推(nvr_time_push_device);此处不批量推(开机时多数未连上)。 */
    ntp_sync_async(s);
    return 0;
}

void nvr_time_tick(nvr_settings_t *s)
{
    time_t now;
    struct tm lt;
    if (!s) return;
    ntp_sync_async(s);
    now = time(NULL);
    localtime_r(&now, &lt);
    if (g_tz_gmtoff != -999999 &&
        (lt.tm_gmtoff != g_tz_gmtoff || lt.tm_isdst != g_tz_isdst)) {
        g_tz_gmtoff = lt.tm_gmtoff;
        g_tz_isdst  = lt.tm_isdst;
        nvr_time_notify_changed(s, "dst_auto");
    }
    /* 每 10 分钟兜底:纠正 IPC 时钟漂移(NVR 未变时也推) */
    static unsigned cam_push_min = 0;
    if (++cam_push_min >= 10) {
        cam_push_min = 0;
        nvr_time_push_cameras(s);
    }
}

int nvr_time_synced(void) { return g_synced; }

/* 手动授时:把给定 UTC epoch 设进 OS 系统时钟 + 写回 RTC + 后台给相机授时。
 * 供 set_datetime 命令用(NVR 为主时间)。设 g_synced=1 让本次会话的 NTP 重试不再覆盖手动值;
 * 是否长期自动纠正由 system.time_sync 开关决定(关=纯手动)。返回 0。 */
int nvr_time_set_clock(nvr_settings_t *s, long long utc_epoch)
{
    struct timeval tv; tv.tv_sec = (time_t)utc_epoch; tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) { NVR_LOGW("time", "settimeofday 失败"); return -1; }
    system("hwclock -w >/dev/null 2>&1");     /* 写回 RTC,断电/重启保留 */
    g_synced = 1;                              /* 本会话 NTP 重试不再覆盖手动时间 */
    time_t now = (time_t)utc_epoch; struct tm lt; localtime_r(&now, &lt);
    NVR_LOGI("time", "手动授时 → 本地 %04d-%02d-%02d %02d:%02d:%02d (已写 RTC)",
             lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    tz_snapshot_update();
    if (s) nvr_time_notify_changed(s, "set_clock");
    return 0;
}

/* 重新允许自动 NTP(TimeSync 开关由关转开时调用):清 g_synced 让 tick 再同步。 */
void nvr_time_resync(nvr_settings_t *s)
{
    g_synced = 0;
    ntp_sync_async(s);
}
