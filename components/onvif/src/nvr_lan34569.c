/* nvr_lan34569 — UDP 34569 LocalLAN 探测 + 本机应答。见 nvr_lan34569.h。 */
#include "nvr_lan34569.h"
#include "nop_sdk/nop_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <fcntl.h>

#define NVR_LAN34569_LOG(...) NOP_LOGI(__VA_ARGS__)

/* DVRIP 搜索：20 字节头，msgid=0x05FA，无 payload。 */
static const unsigned char k_probe[20] = {
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x05,
    0x00, 0x00, 0x00, 0x00
};

static int is_probe(const unsigned char *b, int n)
{
    return n >= 20 && b[0] == 0xff && b[14] == 0xfa && b[15] == 0x05;
}

/* 统一 aa:bb:cc:dd:ee:ff，与通道 chn_by_mac 一致。 */
static int mac_norm(const char *in, char *out, size_t cap)
{
    unsigned char b[6];
    int n = 0, z = 1;
    if (!out || cap < 18) return -1;
    out[0] = 0;
    if (!in) return -1;
    while (*in && n < 6) {
        if (*in == ':' || *in == '-' || *in == '.' || *in == ' ') { in++; continue; }
        if (!isxdigit((unsigned char)in[0]) || !isxdigit((unsigned char)in[1])) break;
        {
            char h[3] = { (char)tolower((unsigned char)in[0]),
                          (char)tolower((unsigned char)in[1]), 0 };
            b[n] = (unsigned char)strtol(h, NULL, 16);
            if (b[n]) z = 0;
            n++;
            in += 2;
        }
    }
    if (n != 6 || z) return -1;
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    return 0;
}

static const char *json_body(const char *buf, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (buf[i] == '{') return buf + i;
    return NULL;
}

static int js_str(const char *j, const char *key, char *out, size_t cap)
{
    char pat[72];
    const char *p;
    size_t n;
    if (!j || !key || !out || cap == 0) return -1;
    out[0] = 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = j;
    while ((p = strstr(p, pat)) != NULL) {
        p += strlen(pat);
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
        if (*p != '"') { p++; continue; }
        p++;
        n = 0;
        while (p[n] && p[n] != '"' && n < cap - 1) n++;
        memcpy(out, p, n);
        out[n] = 0;
        return 0;
    }
    return -1;
}

static int js_int(const char *j, const char *key, int def)
{
    char pat[72];
    const char *p;
    if (!j || !key) return def;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(j, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (!isdigit((unsigned char)*p) && *p != '-') return def;
    return (int)strtol(p, NULL, 10);
}

/* HostIP "0xA700A8C0" = LE 192.168.0.167；已是点分则原样。 */
static void hostip_parse(const char *in, char *out, size_t cap)
{
    unsigned long v;
    if (!in || !out || cap == 0) return;
    out[0] = 0;
    if (!in[0]) return;
    if (in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) {
        v = strtoul(in, NULL, 16);
        snprintf(out, cap, "%u.%u.%u.%u",
                 (unsigned)(v & 0xffu),
                 (unsigned)((v >> 8) & 0xffu),
                 (unsigned)((v >> 16) & 0xffu),
                 (unsigned)((v >> 24) & 0xffu));
        return;
    }
    snprintf(out, cap, "%s", in);
}

static void ip_to_xmhex(const char *ip, char *out, size_t cap)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    unsigned long v;
    if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        snprintf(out, cap, "0x00000000");
        return;
    }
    v = (unsigned long)a | ((unsigned long)b << 8) |
        ((unsigned long)c << 16) | ((unsigned long)d << 24);
    snprintf(out, cap, "0x%08lX", v);
}

static int iface_ipv4(const char *ifname, char *out, size_t cap)
{
    struct ifaddrs *ifa = NULL, *p;
    if (out && cap) out[0] = 0;
    if (!ifname || !out) return -1;
    if (getifaddrs(&ifa) != 0) return -1;
    for (p = ifa; p; p = p->ifa_next) {
        struct sockaddr_in *sin;
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(p->ifa_name, ifname) != 0) continue;
        sin = (struct sockaddr_in *)p->ifa_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t)cap)) {
            freeifaddrs(ifa);
            return 0;
        }
        break;
    }
    freeifaddrs(ifa);
    return -1;
}

static int is_local_ip(const char *ip)
{
    struct ifaddrs *ifa = NULL, *p;
    char buf[64];
    if (!ip || !ip[0]) return 0;
    if (getifaddrs(&ifa) != 0) return 0;
    for (p = ifa; p; p = p->ifa_next) {
        struct sockaddr_in *sin;
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        sin = (struct sockaddr_in *)p->ifa_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) continue;
        if (strcmp(buf, ip) == 0) { freeifaddrs(ifa); return 1; }
    }
    freeifaddrs(ifa);
    return 0;
}

static int parse_dev(const char *json, const char *from_ip, nvr_lan34569_dev_t *d)
{
    char hostip[64], mac[64], sn[64], name[64];
    memset(d, 0, sizeof(*d));
    js_str(json, "MAC", mac, sizeof(mac));
    if (!mac[0]) js_str(json, "mac", mac, sizeof(mac));
    js_str(json, "SN", sn, sizeof(sn));
    if (!sn[0]) js_str(json, "serial", sn, sizeof(sn));
    js_str(json, "HostName", name, sizeof(name));
    if (!name[0]) js_str(json, "name", name, sizeof(name));
    js_str(json, "HostIP", hostip, sizeof(hostip));
    if (!hostip[0]) js_str(json, "ip", hostip, sizeof(hostip));
    hostip_parse(hostip, d->host, sizeof(d->host));
    if (!d->host[0] && from_ip) snprintf(d->host, sizeof(d->host), "%s", from_ip);
    {
        char nm[24];
        if (mac_norm(mac, nm, sizeof(nm)) == 0) snprintf(d->mac, sizeof(d->mac), "%s", nm);
        else snprintf(d->mac, sizeof(d->mac), "%s", mac);
    }
    snprintf(d->serial, sizeof(d->serial), "%s", sn);
    snprintf(d->name, sizeof(d->name), "%s", name);
    {
        char hw[64];
        js_str(json, "Hardware", hw, sizeof(hw));
        if (!hw[0]) js_str(json, "model", hw, sizeof(hw));
        snprintf(d->model, sizeof(d->model), "%s", hw[0] ? hw : name);
    }
    d->http_port = js_int(json, "HttpPort", 0);
    if (d->http_port <= 0) d->http_port = js_int(json, "port", 80);
    return d->host[0] ? 0 : -1;
}

void nvr_lan34569_fill_cam(const nvr_lan34569_dev_t *d, nvr_onvif_cam_t *cam)
{
    char enc[80];
    int i, w = 0;
    memset(cam, 0, sizeof(*cam));
    if (!d) return;
    snprintf(cam->host, sizeof(cam->host), "%s", d->host);
    cam->port = d->http_port > 0 ? d->http_port : 80;
    snprintf(cam->service_url, sizeof(cam->service_url), "/onvif/device_service");
    for (i = 0; d->name[i] && w < (int)sizeof(enc) - 4; i++) {
        if (d->name[i] == ' ') { enc[w++] = '%'; enc[w++] = '2'; enc[w++] = '0'; }
        else if (d->name[i] != '/') enc[w++] = d->name[i];
    }
    enc[w] = 0;
    cam->scopes[0] = 0;
    if (d->mac[0])
        snprintf(cam->scopes + strlen(cam->scopes), sizeof(cam->scopes) - strlen(cam->scopes),
                 "onvif://www.onvif.org/mac/%s ", d->mac);
    if (d->serial[0])
        snprintf(cam->scopes + strlen(cam->scopes), sizeof(cam->scopes) - strlen(cam->scopes),
                 "onvif://www.onvif.org/serial/%s ", d->serial);
    snprintf(cam->scopes + strlen(cam->scopes), sizeof(cam->scopes) - strlen(cam->scopes),
             "onvif://www.onvif.org/hardware/%s onvif://www.onvif.org/name/%s",
             d->model[0] ? d->model : enc, enc);
}

static void send_probe(int fd, const char *dst_ip)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(NVR_LAN34569_PORT);
    inet_pton(AF_INET, dst_ip ? dst_ip : "255.255.255.255", &a.sin_addr);
    sendto(fd, k_probe, (int)sizeof(k_probe), 0, (struct sockaddr *)&a, sizeof(a));
}

int nvr_lan34569_discover(const char *local_ip, int seconds, nvr_lan34569_cb cb, void *user)
{
    int fd, nfound = 0, one = 1;
    char seen[64][64];
    int nseen = 0;
    struct sockaddr_in bind_a;
    struct timeval deadline, now, left;
    char bcast[32];

    if (seconds <= 0) seconds = 1;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&bind_a, 0, sizeof(bind_a));
    bind_a.sin_family = AF_INET;
    bind_a.sin_port = htons(0);
    bind_a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&bind_a, sizeof(bind_a)) != 0) {
        close(fd);
        return -1;
    }

    send_probe(fd, "255.255.255.255");
    if (local_ip && local_ip[0]) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (sscanf(local_ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            snprintf(bcast, sizeof(bcast), "%u.%u.%u.255", a, b, c);
            send_probe(fd, bcast);
        }
    }
    send_probe(fd, "255.255.255.255");

    gettimeofday(&deadline, NULL);
    deadline.tv_sec += seconds;
    while (1) {
        fd_set rfds;
        char buf[2048];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n, i, dup;
        const char *js;
        nvr_lan34569_dev_t dev;

        gettimeofday(&now, NULL);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_usec >= deadline.tv_usec))
            break;
        left.tv_sec = deadline.tv_sec - now.tv_sec;
        left.tv_usec = deadline.tv_usec - now.tv_usec;
        if (left.tv_usec < 0) { left.tv_sec--; left.tv_usec += 1000000; }
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (select(fd + 1, &rfds, NULL, NULL, &left) <= 0) continue;
        n = (int)recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;
        buf[n] = 0;
        js = json_body(buf, n);
        if (!js) continue;
        {
            char fromip[64];
            inet_ntop(AF_INET, &from.sin_addr, fromip, sizeof(fromip));
            if (parse_dev(js, fromip, &dev) != 0) continue;
        }
        if (is_local_ip(dev.host)) continue;
        dup = 0;
        for (i = 0; i < nseen; i++)
            if (strcmp(seen[i], dev.host) == 0) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 64) snprintf(seen[nseen++], sizeof(seen[0]), "%s", dev.host);
        nfound++;
        NVR_LAN34569_LOG("[34569] 发现 %s mac=%s port=%d name=%s",
                         dev.host, dev.mac[0] ? dev.mac : "-",
                         dev.http_port, dev.name[0] ? dev.name : "-");
        if (cb) cb(&dev, user);
    }
    close(fd);
    return nfound;
}

/* ---- 本机应答 ---- */
static pthread_t g_th;
static volatile int g_run;
static int g_fd = -1;
static nvr_lan34569_self_t g_self;

static void build_reply(char *out, size_t cap)
{
    char ip[64] = {0}, hex[20];
    unsigned char hdr[20];
    char json[512];
    unsigned len;
    iface_ipv4("eth0", ip, sizeof(ip));
    if (!ip[0]) snprintf(ip, sizeof(ip), "0.0.0.0");
    ip_to_xmhex(ip, hex, sizeof(hex));
    snprintf(json, sizeof(json),
             "{ \"NetWork.NetCommon\" : { \"HostIP\" : \"%s\", \"HostName\" : \"%s\", "
             "\"HttpPort\" : %d, \"MAC\" : \"%s\", \"SN\" : \"%s\", "
             "\"TCPPort\" : %d, \"UDPPort\" : %d, \"DeviceType\" : 1 }, "
             "\"Ret\" : 100, \"SessionID\" : \"0x00000000\" }",
             hex,
             g_self.name[0] ? g_self.name : "NVR",
             g_self.http_port > 0 ? g_self.http_port : 8089,
             g_self.mac,
             g_self.serial,
             g_self.http_port > 0 ? g_self.http_port : 8089,
             NVR_LAN34569_PORT);
    len = (unsigned)strlen(json);
    memcpy(hdr, k_probe, 20);
    hdr[16] = (unsigned char)(len & 0xff);
    hdr[17] = (unsigned char)((len >> 8) & 0xff);
    hdr[18] = (unsigned char)((len >> 16) & 0xff);
    hdr[19] = (unsigned char)((len >> 24) & 0xff);
    if (20 + len + 1 > cap) { out[0] = 0; return; }
    memcpy(out, hdr, 20);
    memcpy(out + 20, json, len);
    out[20 + len] = 0;
}

static void *server_loop(void *arg)
{
    (void)arg;
    while (g_run) {
        fd_set rfds;
        struct timeval tv;
        unsigned char buf[2048];
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        FD_ZERO(&rfds);
        if (g_fd < 0) break;
        FD_SET(g_fd, &rfds);
        if (select(g_fd + 1, &rfds, NULL, NULL, &tv) <= 0) continue;
        n = (int)recvfrom(g_fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;
        if (!is_probe(buf, n)) continue;
        {
            char pkt[700];
            build_reply(pkt, sizeof(pkt));
            if (pkt[0])
                sendto(g_fd, pkt, 20 + (int)strlen(pkt + 20), 0,
                       (struct sockaddr *)&from, flen);
        }
    }
    return NULL;
}

int nvr_lan34569_server_start(const nvr_lan34569_self_t *self)
{
    struct sockaddr_in a;
    int one = 1;
    if (g_run) return 0;
    if (self) g_self = *self;
    else memset(&g_self, 0, sizeof(g_self));
    if (g_self.http_port <= 0) g_self.http_port = 8089;
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) return -1;
    setsockopt(g_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(g_fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(NVR_LAN34569_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        NVR_LAN34569_LOG("[34569] bind :%d 失败 errno=%d", NVR_LAN34569_PORT, errno);
        close(g_fd); g_fd = -1;
        return -1;
    }
    g_run = 1;
    if (pthread_create(&g_th, NULL, server_loop, NULL) != 0) {
        g_run = 0; close(g_fd); g_fd = -1;
        return -1;
    }
    NVR_LAN34569_LOG("[34569] 本机应答已听 UDP :%d mac=%s", NVR_LAN34569_PORT,
                     g_self.mac[0] ? g_self.mac : "-");
    return 0;
}

void nvr_lan34569_server_stop(void)
{
    if (!g_run) return;
    g_run = 0;
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    pthread_join(g_th, NULL);
}
