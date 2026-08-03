/***************************************************************************************
 *  nvr_netime.c — 网络与时间应用。见 nvr_netime.h。
 *  说明：调用设备上的 busybox 工具(ip/udhcpc/udhcpd/ntpd)。工具缺失则记警告不致命。
 ***************************************************************************************/
#include "nvr_netime.h"
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static int g_synced = 0;

static int run(const char *fmt, ...)
{
    char cmd[512];
    va_list ap; va_start(ap, fmt); vsnprintf(cmd, sizeof(cmd), fmt, ap); va_end(ap);
    NVR_LOGI("net", "$ %s", cmd);
    return system(cmd);
}

/* ---------------- 网络 ---------------- */
int nvr_net_apply(nvr_settings_t *s)
{
    if (!s) return -1;

    /* eth0：默认 DHCP；设了静态则用静态 */
    int eth0_dhcp = nvr_settings_get_int(s, "network.eth0.dhcp", 1);   /* 默认 DHCP */
    if (eth0_dhcp) {
        run("ip link set eth0 up 2>/dev/null");
        run("udhcpc -i eth0 -b -q 2>/dev/null &");   /* 后台租约 */
        NVR_LOGI("net", "eth0 = DHCP");
    } else {
        char ip[32], mask[32], gw[32];
        nvr_settings_get_str(s, "network.eth0.ip",   ip,   sizeof(ip),   "192.168.1.100");
        nvr_settings_get_str(s, "network.eth0.mask", mask, sizeof(mask), "255.255.255.0");
        nvr_settings_get_str(s, "network.eth0.gw",   gw,   sizeof(gw),   "");
        run("ip addr flush dev eth0 2>/dev/null; ip addr add %s/24 dev eth0 2>/dev/null; ip link set eth0 up", ip);
        if (gw[0]) run("ip route add default via %s 2>/dev/null", gw);
        NVR_LOGI("net", "eth0 = 静态 %s", ip);
    }

    /* eth1：PoE 汇聚口 —— 每 PoE 口一个 VLAN(2001..2016)，网段 198.18.<口>.0/24：
     *   · NVR 侧接口取 .1（网关/DHCP 服务地址）
     *   · udhcpd 把 .100..-.199 分给相机；相机落在 198.18.<口>.100，绑定按第 3 段(=口号)入通道口-1
     * conf 运行期生成到 /tmp，自包含无需预置文件。 */
    int poe = nvr_settings_get_int(s, "system.poe_ports", 16);
    int vlan_base = nvr_settings_get_int(s, "network.eth1.vlan_base", 2000);
    run("ip link set eth1 up 2>/dev/null");
    for (int p = 1; p <= poe; p++) {
        int vid = vlan_base + p;
        run("ip link add link eth1 name eth1.%d type vlan id %d 2>/dev/null", vid, vid);
        run("ip addr flush dev eth1.%d 2>/dev/null; ip addr add 198.18.%d.1/24 dev eth1.%d 2>/dev/null; ip link set eth1.%d up 2>/dev/null",
            vid, p, vid, vid);
        /* 生成该 VLAN 的 udhcpd.conf 并起服务 */
        char conf[64]; snprintf(conf, sizeof(conf), "/tmp/udhcpd.eth1.%d.conf", vid);
        FILE *f = fopen(conf, "w");
        if (f) {
            fprintf(f,
                "interface eth1.%d\n"
                "start 198.18.%d.100\n"
                "end 198.18.%d.199\n"
                "option subnet 255.255.255.0\n"
                "option router 198.18.%d.1\n"
                "max_leases 100\n"
                "lease_file /tmp/udhcpd.eth1.%d.leases\n"
                "pidfile /tmp/udhcpd.eth1.%d.pid\n",
                vid, p, p, p, vid, vid);
            fclose(f);
            run("udhcpd %s 2>/dev/null &", conf);
        }
    }
    NVR_LOGI("net", "eth1 PoE 汇聚: %d 个 VLAN(%d..%d), NVR 侧 198.18.<口>.1, 相机 DHCP .100+",
             poe, vlan_base + 1, vlan_base + poe);
    return 0;
}

/* ---------------- 时间（UTC 内部时钟 + 时区显示 + NTP） ---------------- */
int nvr_time_apply(nvr_settings_t *s)
{
    if (!s) return -1;
    char tz[64], ntp[128];
    nvr_settings_get_str(s, "system.timezone", tz,  sizeof(tz),  "UTC");
    nvr_settings_get_str(s, "system.ntp",      ntp, sizeof(ntp), "pool.ntp.org");

    /* 时区仅影响显示；系统时钟保持 UTC。设 TZ + /etc/localtime(若有 zoneinfo) */
    setenv("TZ", tz, 1); tzset();
    run("[ -f /usr/share/zoneinfo/%s ] && ln -sf /usr/share/zoneinfo/%s /etc/localtime 2>/dev/null", tz, tz);
    NVR_LOGI("time", "时区=%s (系统时钟 UTC), NTP=%s", tz, ntp);

    /* 触发一次 NTP 同步（busybox ntpd -q 一次性；-n 前台）。设备联网后成功。 */
    int rc = run("ntpd -q -n -p %s 2>/dev/null", ntp);
    if (rc == 0) { g_synced = 1; NVR_LOGI("time", "NTP 同步成功: %s", ntp); }
    else         NVR_LOGW("time", "NTP 未成功(离线?)，稍后重试");
    return 0;
}

void nvr_time_tick(nvr_settings_t *s)
{
    if (g_synced || !s) return;
    char ntp[128]; nvr_settings_get_str(s, "system.ntp", ntp, sizeof(ntp), "pool.ntp.org");
    if (run("ntpd -q -n -p %s 2>/dev/null", ntp) == 0) { g_synced = 1; NVR_LOGI("time", "NTP 同步成功(重试)"); }
}

int nvr_time_synced(void) { return g_synced; }
