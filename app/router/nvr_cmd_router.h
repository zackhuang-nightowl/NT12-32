/***************************************************************************************
 *  nvr_cmd_router.h — NVR 命令路由（8089 界面命令分流）
 *
 *  界面 POST http://127.0.0.1:8089/APPJsonCmd 的 NOP JSON 进本路由：
 *    1) NVR 本地命令 → 读写 **设置库**（该 NVR 记录的记录存起来）+ 查真实子系统。
 *    2) 面向通道的相机命令 → 按 args.channel 用通道表找到**真实设备 IP**：
 *         kind=NOP     → 透传：HTTP POST 原样转发到设备 发现口/APPJsonCmd
 *         kind=onvif/nopOnvif → 翻译：走 nopcore 映射(nop_app_dispatch)
 *    3) 其它 → 回落 nop_app_dispatch（nopcore 293 handler）。
 ***************************************************************************************/
#ifndef NVR_CMD_ROUTER_H
#define NVR_CMD_ROUTER_H

#include "nvr_settings.h"
#include "nvr_channel.h"
#include "nvr_storage.h"     /* nvr_storage_t（存储命令接真实盘） */
#include "rsdk.h"            /* rsdk_group_t（事件/回放查询） */
#include "nop_sdk/nop_app.h"
#include "nvr_preview.h"
#include "nvr_chan_persist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_cmd_router nvr_cmd_router_t;

typedef struct {
    nvr_settings_t *settings;   /* 本地命令持久化 */
    nvr_chan_mgr_t *cm;         /* channel→设备 解析 */
    nvr_storage_t  *stg;        /* 存储信息/格式化/健康 */
    struct nvr_stream_mgr *sm;  /* 拉流/录像管理器:格式化后重组装盘组→补开 writer */
    rsdk_group_t   *group;      /* 事件/录像 查询 */
    void           *meta;       /* rsdk_meta ctx（事件元数据）；可 NULL */
    nop_app_t      *nop;        /* 回落 */
    int             port;       /* 默认 8089 */
    int             dev_nop_port; /* 转发到设备的 NOP 端口，默认 8089 */
    nvr_preview_t      *pv;        /* 出图：显示指令驱动 */
    struct nvr_playback *pb;       /* 本机回放引擎 */
    struct nvr_evt_hub *eh;        /* 事件中枢(longPolling 事件位图) */
    nvr_chan_persist_t *persist;   /* 通道映射/能力持久化 */
    struct nvr_talk    *talk;      /* 双向对讲(本机 127.0.0.1:7000) */
    void  *disp_user;              /* 分辨率热切回调上下文(app) */
    void (*on_set_resolution)(void *disp_user, int w, int h);  /* setSysDisplay 热切 */
} nvr_cmd_router_cfg_t;

int  nvr_cmd_router_start(const nvr_cmd_router_cfg_t *cfg, nvr_cmd_router_t **out);
void nvr_cmd_router_stop (nvr_cmd_router_t *r);

/* 纯函数：一条 NOP JSON → 应答(malloc；调用方 free)。可主机单测(不起 HTTP)。 */
char *nvr_cmd_dispatch(nvr_cmd_router_t *r, const char *json_in);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CMD_ROUTER_H */
