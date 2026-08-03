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

/* 周期维护：NTP 未成功则重试（app 主循环每隔一段调）。 */
void nvr_time_tick(nvr_settings_t *s);

/* 是否已成功 NTP 同步。 */
int  nvr_time_synced(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_NETIME_H */
