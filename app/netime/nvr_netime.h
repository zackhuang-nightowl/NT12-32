/***************************************************************************************
 *  nvr_netime.h — 网络与时间：eth0 DHCP/静态、eth1 PoE VLAN+DHCP 服务、NTP(UTC)、时区
 *
 *  固件启动时把 system.json/设置库 的网络与时间配置**应用到系统**：
 *    - eth0：DHCP(默认) 或静态；eth1：PoE 汇聚口 + 每口 VLAN + udhcpd 给相机分 198.18.<口>.100
 *    - 时间：NTP 同步(UTC 内部时钟) + 时区(显示用)；可选把时间推给 ONVIF 相机(SetSystemDateAndTime)
 *  内部一律 UTC(录像/云存 starttime 用 epoch UTC)，显示按 timezone 换算。
 ***************************************************************************************/
#ifndef NVR_NETIME_H
#define NVR_NETIME_H

#include "nvr_settings.h"
#include "nvr_onvif.h"   /* nvr_onvif_time_cfg_t */

#ifdef __cplusplus
extern "C" {
#endif

/* 应用网络配置（读设置库；调用 BusyBox ifconfig/route/vconfig/udhcpc/udhcpd）。返回 0。 */
int  nvr_net_apply(nvr_settings_t *s);

/* eth0 管理口:读 Linux 实时状态 + 设置库合并 → out; 返回 0/-1。 */
int  nvr_net_local_link_fill(nvr_settings_t *s, nvr_local_link_t *out);

/* 持久化 local_link + 同步 eth0 KV + 应用 eth0/DNS(不碰 eth1 PoE)。返回 0/-1。 */
int  nvr_net_local_link_apply(nvr_settings_t *s, const nvr_local_link_t *in);

/* 仅应用 eth0(DHCP/静态 + 默认路由);供开机 nvr_net_apply 与 setLocalLink 共用。 */
int  nvr_net_apply_eth0(nvr_settings_t *s);

/* 读 eth0 链路速率(Mbps);失败返回 -1。 */
int  nvr_net_eth0_link_mbps(void);

#define NVR_WAN_IF_MAX 4

typedef struct {
    char value[8];                         /* 当前 WAN 类型: "eth" | "wifi" */
    const char *list[NVR_WAN_IF_MAX];      /* 可用类型列表 */
    int list_n;
    const char *connected[NVR_WAN_IF_MAX]; /* 已连接类型 */
    int conn_n;
} nvr_wan_if_info_t;

/* 读 Linux /sys/class/net 填充 WAN 接口(value/list/connected)。返回 0/-1。 */
int  nvr_net_wan_fill(nvr_wan_if_info_t *out);

/* LAN 物理/最大接收带宽(Mbps, eth0);失败 -1。 */
int  nvr_net_lan_bandwidth_mbps(int *total_mbps, int *max_rx_mbps);

struct nvr_chan_mgr;

typedef struct {
    int  enable;
    int  http_port;
    int  tcp_port;
    int  running;                 /* Linux 上 miniupnpd 是否在跑(只读) */
} nvr_upnp_cfg_t;

/* UPnP:读设置库 + 探测 miniupnpd; apply 启停服务。返回 0/-1。 */
int  nvr_net_upnp_fill(nvr_settings_t *s, nvr_upnp_cfg_t *out);
int  nvr_net_upnp_apply(nvr_settings_t *s);

/* PoE 口 channel=1..16:读 Linux VLAN 状态 + 通道在线估算功耗; set 启停 VLAN。 */
int  nvr_net_poe_fill(struct nvr_chan_mgr *cm, nvr_settings_t *s, int channel,
                      int *enable, int *power_used);
int  nvr_net_poe_apply(nvr_settings_t *s, int channel, int enable);

/* SMTP 测试邮件(明文 AUTH;465/SSL 暂不支持)。成功 0,失败 -1。 */
int  nvr_net_email_test(const nvr_email_cfg_t *cfg, const char *receiver);

/* 应用时区 + 触发 NTP；时区装完后立即推 IPC，NTP 成功后再推一次。 */
int  nvr_time_apply(nvr_settings_t *s);

/* 仅安装时区（不含 NTP）：由 timezone/tz_dst 生成合法 POSIX → TZif(/flash) → /etc/localtime
 * 软链 + 本进程 tzset。供 setTimezone 命令即时生效(GUI 与 nvr_app 两进程都读 /etc/localtime)。 */
int  nvr_tz_install(nvr_settings_t *s);

/* 解析 "GMT ±H:MM" → 显示偏移分钟(东为正,如 UTC+8 → 480)。失败返回 -1。 */
int  nvr_tz_parse_gmt_offset_min(const char *timezone);

/* 该标准时区偏移是否允许开启夏令(GUI 24 区中观测 DST 的区域)。 */
int  nvr_tz_offset_supports_dst(int offset_min);

/* setTimezone 校验: 0=通过; -1=tz_dst 格式非法; -2=该时区不支持夏令。 */
int  nvr_tz_validate_set(const char *timezone, const char *tz_dst);

/* 按 NVR 设置填充 ONVIF 授时用的 TimeZone/DaylightSavings。 */
void nvr_time_build_onvif_cfg(nvr_settings_t *s, nvr_onvif_time_cfg_t *cfg);

/* 把 NVR 当前时间经 ONVIF 下发所有已添加相机（后台异步，不阻塞）。 */
int  nvr_time_push_cameras(nvr_settings_t *s);

/* 单台相机授时(出图/新添加时立刻推;后台异步,不阻塞 tick)。 */
int  nvr_time_push_device(nvr_settings_t *s, const char *ip, int port,
                          const char *user, const char *pass);

/* NVR 时钟/时区(含夏令)有效状态变化 → 立即推 IPC 一次(内部 5s debounce)。 */
void nvr_time_notify_changed(nvr_settings_t *s, const char *reason);

/* 手动授时：把 UTC epoch 设进系统时钟 + 写 RTC + 给相机授时（set_datetime 用；NVR 为主时间）。 */
int  nvr_time_set_clock(nvr_settings_t *s, long long utc_epoch);

/* 由"自动授时开关"关→开时调用：清同步标志并立即再试 NTP。 */
void nvr_time_resync(nvr_settings_t *s);

/* 周期维护：NTP 重试 + 夏令自动切换检测 + 10min IPC 漂移兜底。 */
void nvr_time_tick(nvr_settings_t *s);

/* 是否已成功 NTP 同步。 */
int  nvr_time_synced(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_NETIME_H */
