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

#ifdef __cplusplus
extern "C" {
#endif

/* 应用网络配置（读设置库；调用 busybox ip/udhcpc/udhcpd）。返回 0。 */
int  nvr_net_apply(nvr_settings_t *s);

/* 应用时区 + 触发一次 NTP 同步（UTC 系统时钟）。返回 0。 */
int  nvr_time_apply(nvr_settings_t *s);

/* 仅安装时区（不含 NTP）：由 timezone/tz_dst 生成合法 POSIX → TZif(/flash) → /etc/localtime
 * 软链 + 本进程 tzset。供 setTimezone 命令即时生效(GUI 与 nvr_app 两进程都读 /etc/localtime)。 */
int  nvr_tz_install(nvr_settings_t *s);

/* 把 NVR 当前时间经 ONVIF 下发所有已添加相机（后台异步，不阻塞）。改时区/改时间时触发。 */
int  nvr_time_push_cameras(nvr_settings_t *s);

/* 手动授时：把 UTC epoch 设进系统时钟 + 写 RTC + 给相机授时（set_datetime 用；NVR 为主时间）。 */
int  nvr_time_set_clock(nvr_settings_t *s, long long utc_epoch);

/* 由"自动授时开关"关→开时调用：清同步标志并立即再试 NTP。 */
void nvr_time_resync(nvr_settings_t *s);

/* 周期维护：NTP 未成功则重试（app 主循环每隔一段调）。 */
void nvr_time_tick(nvr_settings_t *s);

/* 是否已成功 NTP 同步。 */
int  nvr_time_synced(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_NETIME_H */
