/***************************************************************************************
 *  nvr_lan34569.h — UDP 34569 LocalLAN（DVRIP/NetSurveillance 搜索）
 *
 *  用途（docs/BIND_IPC_FLOW.md §6）：
 *    · 扫网：广播探测相机，回 IP/MAC/机型（WS-Discovery 不可达时的备用发现）
 *    · 找回：已添加设备按 MAC 匹配当前 IP（DHCP/换网段）
 *    · 本机应答：App 忘 NVR IP 时同样用 34569 搜到本机
 ***************************************************************************************/
#ifndef NVR_LAN34569_H
#define NVR_LAN34569_H

#include "nvr_onvif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_LAN34569_PORT 34569

typedef struct {
    char host[64];
    char mac[24];
    char serial[64];
    char name[64];
    char model[64];
    int  http_port;
} nvr_lan34569_dev_t;

typedef void (*nvr_lan34569_cb)(const nvr_lan34569_dev_t *dev, void *user);

/* 本机应答用的身份（IP 每次从 eth0 现取，DHCP 变更后仍正确）。 */
typedef struct {
    char mac[24];
    char serial[64];
    char name[64];
    char model[64];
    int  http_port;     /* NOP 8089 */
} nvr_lan34569_self_t;

/* 广播探测 seconds 秒，每台回调一次（同 IP 去重）。local_ip 可空。返回发现数。 */
int  nvr_lan34569_discover(const char *local_ip, int seconds,
                           nvr_lan34569_cb cb, void *user);

/* 填成 nvr_onvif_cam_t（scopes 带 /mac/ /serial/ /hardware/ /name/），供 on_discovered。 */
void nvr_lan34569_fill_cam(const nvr_lan34569_dev_t *d, nvr_onvif_cam_t *cam);

/* 听 34569，收到搜索包就回本机 NetWork.NetCommon。0=ok。 */
int  nvr_lan34569_server_start(const nvr_lan34569_self_t *self);
void nvr_lan34569_server_stop(void);

#ifdef __cplusplus
}
#endif
#endif
