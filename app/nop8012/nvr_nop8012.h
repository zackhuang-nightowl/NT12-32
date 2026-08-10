/***************************************************************************************
 *  nvr_nop8012.h — NOP 8012 事件中心客户端(XVR 侧)。
 *
 *  作为 XVR 登录每个 NOP(kind==0)相机的 8012 事件中心(TCP,固定端口 8012),长连接 +
 *  30s 心跳 + 断线重连;收相机主动上报的 eMSG_CMD_SEND_MSG(msgType + 可选 JPEG),
 *  归一化后 nop_event_publish 到共享事件脊柱(nop_event_hub) → app 订阅做本地扇出
 *  (事件录像 + 缩略图 + 图标 + longPolling)。
 *
 *  单个 reactor 线程 + poll() 管所有 NOP 通道 socket;通道来源为 nvr_chan_mgr(每轮重扫,
 *  拾取 LanAddDevice 新增 / LanDelDevice 删除)。协议编解码见 nvr_nop8012_proto.h;
 *  msgType→detect 归类见 nvr_evt_types.h(单一事件类型表)。
 ***************************************************************************************/
#ifndef NVR_NOP8012_H
#define NVR_NOP8012_H

#include "nvr_channel.h"                /* nvr_chan_mgr_t / nvr_chan_list / nvr_channel_t */
#include "nvr_settings.h"               /* nvr_settings_owner_get(ownerId 回退密码) */
#include "nop_sdk/nop_event.h"          /* nop_event_hub_t / nop_event_publish */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_nop8012 nvr_nop8012_t;

typedef struct {
    nvr_chan_mgr_t  *cm;        /* 通道来源(仅 kind==NOP 的通道纳入) */
    nvr_settings_t  *settings;  /* ownerId 回退密码(可 NULL) */
    nop_event_hub_t *hub;       /* 事件发布目的(必需) */
    int              port;      /* 8012 端口,<=0 默认 8012 */
} nvr_nop8012_cfg_t;

/* 起 reactor 线程。成功返回 0 并置 *out。 */
int  nvr_nop8012_start(const nvr_nop8012_cfg_t *cfg, nvr_nop8012_t **out);
/* 停线程 + 关所有连接 + 释放。 */
void nvr_nop8012_stop (nvr_nop8012_t *c);

#ifdef __cplusplus
}
#endif
#endif /* NVR_NOP8012_H */
