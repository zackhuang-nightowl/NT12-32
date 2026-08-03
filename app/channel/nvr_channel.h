/***************************************************************************************
 *  nvr_channel.h — 通道管理 + 在线状态机（app 集成层，计划 §B1）
 *
 *  职责：持有权威每通道表；唯一调用 nvr_stream_* 的模块；解析待定 ONVIF URL；
 *        跑 在线/掉线/重连 状态机；动态增删/绑定（PoE 即插即用 + LAN Add）。
 *  解耦：通过回调通知上层（preview 映射窗口 / record_sched 开关录像），不横向 include。
 ***************************************************************************************/
#ifndef NVR_CHANNEL_H
#define NVR_CHANNEL_H

#include "nvr_config.h"       /* nvr_channel_t / nvr_config_t */
#include "nvr_streaming.h"    /* nvr_stream_mgr_t / nvr_ch_state_t */
#include "nvr_chan_status.h"  /* nvr_conn_t / nvr_chan_substate_t / nvr_chan_status_code */

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_MAX_CH     32     /* 整机通道上限（16 PoE + 16 数字） */
#define NVR_POE_PORTS  16     /* PoE 口数 */
#define NVR_IP_CH_BASE 16     /* 数字/LAN 通道起始号（PoE 之后） */

/* app 级状态（比 nvr_ch_state_t 更贴业务） */
typedef enum {
    NVR_CHAN_EMPTY, NVR_CHAN_BOUND, NVR_CHAN_CONNECTING,
    NVR_CHAN_ONLINE, NVR_CHAN_NOSIGNAL, NVR_CHAN_FAIL, NVR_CHAN_DISABLED
} nvr_chan_status_t;

typedef struct nvr_chan_mgr nvr_chan_mgr_t;

typedef struct {
    nvr_stream_mgr_t *sm;                 /* borrowed：唯一取流管理器 */
    int   reconnect_base_s;               /* 重连基础退避秒，默认 5 */
    int   reconnect_max_s;                /* 最大退避秒，默认 30 */
    void *user;                           /* 回调上下文 */
    void (*on_online)(void *user, int chn);   /* 通道上线（→preview 映射 + rec 开写） */
    void (*on_offline)(void *user, int chn);  /* 通道掉线（→preview 无信号 + rec 停写） */
} nvr_chan_mgr_cfg_t;

int  nvr_chan_mgr_init  (const nvr_chan_mgr_cfg_t *cfg, nvr_chan_mgr_t **out);
void nvr_chan_mgr_deinit(nvr_chan_mgr_t *m);

/* 批量载入（替代 nvr_app 内联 add_channel 循环）：把 cfg->ch[] 加入并起流。 */
int  nvr_chan_load_config(nvr_chan_mgr_t *m, const nvr_config_t *cfg);

/* 动态增删/绑定 */
int  nvr_chan_add        (nvr_chan_mgr_t *m, const nvr_channel_t *desc); /* 返回 chn 或 -1 */
int  nvr_chan_remove     (nvr_chan_mgr_t *m, int chn);
int  nvr_chan_bind_poe   (nvr_chan_mgr_t *m, int poe_port,
                          const char *ip, const char *user, const char *pass);
/* 发现结果落地：按 scopes 分类并写 kind/backend（见 nvr_dev_classify）。 */
int  nvr_chan_apply_discovery(nvr_chan_mgr_t *m, int chn, const char *scopes);

/* PoE/LAN 自动发现绑定：跑一次 ONVIF WS-Discovery(在 local_ip 网段, seconds 秒)，
 * 逐台分类(nvr_dev_classify) → 分配通道(198.18.N.100→PoE口 N；否则数字通道) → 绑定 →
 * 通道 tick 会经 nvr_onvif_get_url 取流出图。返回新发现绑定数。local_ip 可为 NULL(默认接口)。 */
int  nvr_chan_run_discovery(nvr_chan_mgr_t *m, const char *local_ip, int seconds);

int  nvr_chan_start_all(nvr_chan_mgr_t *m);
void nvr_chan_stop_all (nvr_chan_mgr_t *m);

/* 周期：轮询流状态 → 跑 FSM → 退避重连 → 解析待定 ONVIF URL。app 主循环每秒调。 */
void nvr_chan_tick(nvr_chan_mgr_t *m);

/* 查询（供 preview OSD / NOP handler / 诊断） */
int  nvr_chan_get   (nvr_chan_mgr_t *m, int chn, nvr_channel_t *out);
int  nvr_chan_list  (nvr_chan_mgr_t *m, nvr_channel_t *out, int cap);
nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t *m, int chn);
const char *nvr_chan_status_name(nvr_chan_status_t s);

/* 子状态（鉴权失败/超解码预算/待激活/休眠/固件升级）：由信号点置位，供 0-7 状态码合成。 */
void nvr_chan_set_substate(nvr_chan_mgr_t *m, int chn, const nvr_chan_substate_t *sub);
/* 取该通道 getChannelStatus 用的 0-7 码：FSM→conn 映射 + 子状态 → nvr_chan_status_code。 */
int  nvr_chan_status_code_of(nvr_chan_mgr_t *m, int chn);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CHANNEL_H */
