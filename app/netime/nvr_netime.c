/***************************************************************************************
 *  nvr_netime.c — 网络与时间应用。见 nvr_netime.h。
 *  说明：调用设备上的 busybox 工具(ip/udhcpc/udhcpd/ntpd)。工具缺失则记警告不致命。
 ***************************************************************************************/
#include "nvr_netime.h"
#include "nvr_defaults.h"
#include "nvr_log.h"
#include "nvr_onvif.h"      /* nvr_onvif_set_time_now:给相机 ONVIF 授时 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

static int g_synced = 0;
static volatile int g_ntp_busy = 0;   /* 一次只跑一个 ntpd 线程,防堆积 */

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
        nvr_settings_get_str(s, "network.eth0.ip",   ip,   sizeof(ip),   NVR_DEF_ETH0_IP);
        nvr_settings_get_str(s, "network.eth0.mask", mask, sizeof(mask), NVR_DEF_ETH0_MASK);
        nvr_settings_get_str(s, "network.eth0.gw",   gw,   sizeof(gw),   "");
        run("ip addr flush dev eth0 2>/dev/null; ip addr add %s/24 dev eth0 2>/dev/null; ip link set eth0 up", ip);
        if (gw[0]) run("ip route add default via %s 2>/dev/null", gw);
        NVR_LOGI("net", "eth0 = 静态 %s", ip);
    }

    /* eth1：PoE 汇聚口 —— 每 PoE 口一个 VLAN(2001..2016)，网段 198.18.<口>.0/24。
     * ⚠️ 与原厂(S30eth1vlan + dhcpd_vlan.conf)对齐：
     *   · NVR 侧接口取 .100（网关/DHCP 服务地址）— 原厂 SET_ETH_IP_2 2001 198.18.1.100
     *   · udhcpd 只发 198.18.<口>.1（单地址），option router/dns = .100
     *     相机固定落在 198.18.<口>.1；按第 3 段(=口号)入通道 口-1
     *   （原厂用 ISC dhcpd + NOIPC host-name class 只认自家相机；此处 busybox udhcpd
     *     以单地址池近似，NightOwl PoE 相机上报主机名 NOIPC 亦兼容）
     * conf 运行期生成到 /tmp，自包含无需预置文件。 */
    int poe = nvr_settings_get_int(s, "system.poe_ports", 16);
    int vlan_base = nvr_settings_get_int(s, "network.eth1.vlan_base", NVR_DEF_VLAN_BASE);
    run("ip link set eth1 up 2>/dev/null");
    for (int p = 1; p <= poe; p++) {
        int vid = vlan_base + p;
        run("ip link add link eth1 name eth1.%d type vlan id %d 2>/dev/null", vid, vid);
        run("ip addr flush dev eth1.%d 2>/dev/null; ip addr add 198.18.%d.100/24 dev eth1.%d 2>/dev/null; ip link set eth1.%d up 2>/dev/null",
            vid, p, vid, vid);
        /* 生成该 VLAN 的 udhcpd.conf 并起服务：相机固定 .1，网关/DNS 指向 NVR .100 */
        char conf[64]; snprintf(conf, sizeof(conf), "/tmp/udhcpd.eth1.%d.conf", vid);
        FILE *f = fopen(conf, "w");
        if (f) {
            fprintf(f,
                "interface eth1.%d\n"
                "start 198.18.%d.1\n"
                "end 198.18.%d.1\n"
                "option subnet 255.255.255.0\n"
                "option router 198.18.%d.100\n"
                "option dns 198.18.%d.100\n"
                "max_leases 1\n"
                "lease_file /tmp/udhcpd.eth1.%d.leases\n"
                "pidfile /tmp/udhcpd.eth1.%d.pid\n",
                vid, p, p, p, p, vid, vid);
            fclose(f);
            run("udhcpd %s 2>/dev/null &", conf);
        }
    }
    NVR_LOGI("net", "eth1 PoE 汇聚: %d 个 VLAN(%d..%d), NVR 侧 198.18.<口>.100, 相机固定 198.18.<口>.1",
             poe, vlan_base + 1, vlan_base + poe);
    return 0;
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

/* 把 system.timezone("GMT ±H:MM")/ system.tz_dst(POSIX DST 串) 转成合法 POSIX TZ 串。
 * tz_dst 若非空按协议优先(已是 POSIX,原样用);否则由 GMT 偏移换算(注意 POSIX 符号相反:
 * 显示 UTC-5 → POSIX 偏移 +5 → "GMT5";显示 UTC+8 → "GMT-8")。 */
static void build_posix_tz(nvr_settings_t *s, char *out, int cap)
{
    char dst[80] = {0};
    nvr_settings_get_str(s, "system.tz_dst", dst, sizeof(dst), "");
    if (dst[0]) { snprintf(out, cap, "%s", dst); return; }

    char tz[64]; nvr_settings_get_str(s, "system.timezone", tz, sizeof(tz), NVR_DEF_TIMEZONE);
    const char *p = tz;
    if (strncmp(p, "GMT", 3) == 0) p += 3;
    while (*p == ' ') p++;
    int sign = 1;
    if      (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    int H = 0, M = 0; sscanf(p, "%d:%d", &H, &M);
    int posix_min = -(sign * (H * 60 + M));       /* POSIX 偏移符号与显示相反 */
    int ah = (posix_min < 0 ? -posix_min : posix_min) / 60;
    int am = (posix_min < 0 ? -posix_min : posix_min) % 60;
    snprintf(out, cap, "GMT%s%d:%02d", posix_min < 0 ? "-" : "", ah, am);
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
typedef struct { int n; nvr_camera_row_t rows[NVR_TZ_CAM_CAP]; } cam_push_ctx_t;

static void *cam_push_thread(void *arg)
{
    cam_push_ctx_t *c = (cam_push_ctx_t *)arg;
    int ok = 0;
    for (int i = 0; i < c->n; i++) {
        nvr_camera_row_t *r = &c->rows[i];
        if (!r->enabled || !r->ip[0]) continue;
        int port = r->onvif_port > 0 ? r->onvif_port : 80;
        if (nvr_onvif_set_time_now(r->ip, port, r->username, r->password) == 0) ok++;
        else NVR_LOGW("time", "相机授时失败 ip=%s", r->ip);
    }
    NVR_LOGI("time", "相机 ONVIF 授时完成: %d/%d 成功", ok, c->n);
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
    pthread_t th;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, cam_push_thread, ctx) != 0) { pthread_attr_destroy(&at); free(ctx); return -1; }
    pthread_attr_destroy(&at);
    NVR_LOGI("time", "相机 ONVIF 授时: 后台下发 %d 台", ctx->n);
    return 0;
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
static void *ntp_sync_thread(void *arg)
{
    char *ntp1 = (char *)arg;
    FILE *f = fopen("/tmp/nvr_ntp.conf", "w");
    if (f) {
        fprintf(f, "server %s iburst\nserver %s iburst\nserver %s iburst\n",
                ntp1, NVR_DEF_NTP2, NVR_DEF_NTP3);
        fclose(f);
    }
    int rc = system("ntpd -gq -n -c /tmp/nvr_ntp.conf >/dev/null 2>&1");
    if (rc == 0) {
        system("hwclock -w >/dev/null 2>&1");    /* 系统时(UTC)写回 RTC */
        g_synced = 1;
        time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
        NVR_LOGI("time", "NTP 同步成功 → 本地 %04d-%02d-%02d %02d:%02d:%02d (已写 RTC)",
                 lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    } else {
        NVR_LOGW("time", "NTP 未成功(离线/服务器不可达?), 稍后重试");
    }
    g_ntp_busy = 0;
    free(ntp1);
    return NULL;
}

/* 触发一次异步 NTP 授时。门控:自动授时开关关(system.time_sync=0=手动)/已同步/已有线程在跑 → 跳过。 */
static void ntp_sync_async(nvr_settings_t *s)
{
    if (!s || g_synced || g_ntp_busy) return;
    if (!nvr_settings_get_int(s, "system.time_sync", 1)) return;   /* 手动模式:不跑 NTP,不覆盖手动时间 */
    char *ntp1 = (char *)calloc(1, 128);
    if (!ntp1) return;
    nvr_settings_get_str(s, "system.ntp", ntp1, 128, NVR_DEF_NTP1);
    g_ntp_busy = 1;
    pthread_t th; pthread_attr_t at;
    pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, ntp_sync_thread, ntp1) != 0) { g_ntp_busy = 0; free(ntp1); }
    pthread_attr_destroy(&at);
}

int nvr_time_apply(nvr_settings_t *s)
{
    if (!s) return -1;
    nvr_tz_install(s);   /* 时区:合法 POSIX + TZif + /etc/localtime + tzset */
    ntp_sync_async(s);   /* NTP:异步,绝不阻塞开机(受 time_sync 开关门控) */
    return 0;
}

void nvr_time_tick(nvr_settings_t *s)
{
    ntp_sync_async(s);   /* 未同步则再试(内部按 time_sync/g_synced/g_ntp_busy 门控) */
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
    if (s) nvr_time_push_cameras(s);           /* 以 NVR 为主时间下发相机 */
    return 0;
}

/* 重新允许自动 NTP(TimeSync 开关由关转开时调用):清 g_synced 让 tick 再同步。 */
void nvr_time_resync(nvr_settings_t *s)
{
    g_synced = 0;
    ntp_sync_async(s);
}
