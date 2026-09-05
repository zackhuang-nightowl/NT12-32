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

typedef struct nop_onvif_map_backend nop_onvif_map_backend_t;

typedef struct {
    nvr_stream_mgr_t *sm;                 /* borrowed：唯一取流管理器 */
    nvr_settings_t   *settings;           /* borrowed：设备落库(camera 表);可空(不持久化) */
    struct nop_nvr_channels *nop_chans;     /* borrowed：ONVIF 映射注册表；可空(不同步) */
    nop_onvif_map_backend_t *onvif_be;      /* borrowed：ONVIF 会话缓存；可空(不刷新) */
    struct nop_app          *nop;           /* borrowed：nop 进程内分派(ONVIF 能力集经映射收集入库);可空 */
    int   reconnect_base_s;               /* 重连基础退避秒，默认 5 */
    int   reconnect_max_s;                /* 最大退避秒，默认 30 */
    void *user;                           /* 回调上下文 */
    void (*on_online)(void *user, int chn);   /* 通道上线（→preview 映射 + rec 开写） */
    void (*on_offline)(void *user, int chn);  /* 通道掉线（→preview 无信号 + rec 停写） */
} nvr_chan_mgr_cfg_t;

int  nvr_chan_mgr_init  (const nvr_chan_mgr_cfg_t *cfg, nvr_chan_mgr_t **out);
void nvr_chan_mgr_deinit(nvr_chan_mgr_t *m);
/** 迟绑 nop_nvr_channels（nvr_app 在创建映射后端后注入）。 */
void nvr_chan_mgr_set_nop_registry(nvr_chan_mgr_t *m, struct nop_nvr_channels *reg);
/** 迟绑 ONVIF 映射 backend（与 nop_chans 同时注入）。 */
void nvr_chan_mgr_set_onvif_backend(nvr_chan_mgr_t *m, nop_onvif_map_backend_t *be);

/* 批量载入（替代 nvr_app 内联 add_channel 循环）：把 cfg->ch[] 加入并起流。 */
int  nvr_chan_load_config(nvr_chan_mgr_t *m, const nvr_config_t *cfg);

/* 动态增删/绑定 */
int  nvr_chan_add        (nvr_chan_mgr_t *m, const nvr_channel_t *desc); /* 返回 chn 或 -1 */
int  nvr_chan_remove     (nvr_chan_mgr_t *m, int chn);
/* setLanDevice 权威落库账密/enh/端口(无条件写库,凭据变则触发重解析;不重装流)。返回 1=变/0=无变/-1=错。 */
int  nvr_chan_set_creds  (nvr_chan_mgr_t *m, int chn, const char *user, const char *pass,
                          const char *enh_random, int enh_on, int onvif_port);
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

/* 切通道主/子码流(供 preview 按 单宫格=主 / 多宫格=子 调)：stream=NVR_STREAM_MAIN/SUB。
 * 停当前 puller、置新 stream、清 url 强制按新码流经 ONVIF 重解析,由 tick 重连出图。
 * 已是该码流且已解析则不动。返回 0/-1。 */
int  nvr_chan_set_stream(nvr_chan_mgr_t *m, int chn, int stream);

/* 读并**清全部**"状态变化位图"(bit=chn)。仅 refresh 全量重同步用。 */
unsigned nvr_chan_drain_notify(nvr_chan_mgr_t *m);
/* 只读位图,**不清**(锁存上报;longPolling 用)。 */
unsigned nvr_chan_peek_notify(nvr_chan_mgr_t *m);
/* GUI 经 getChannelStatus 收取某通道 → 清该位(锁存解除)。 */
void     nvr_chan_clear_notify(nvr_chan_mgr_t *m, int chn);
/* 挂起到有状态变化或 timeout_ms;**不清 notify**(锁存)。timeout_ms<=0 立即返回当前位图。 */
unsigned nvr_chan_wait_notify(nvr_chan_mgr_t *m, int timeout_ms);
/* 唤醒挂起的 GUI_longPolling(向导页跳转等,不置 ChannelStatusNotify 位)。 */
void nvr_chan_poke_longpoll(nvr_chan_mgr_t *m);

/* 查询（供 preview OSD / NOP handler / 诊断） */
int  nvr_chan_get   (nvr_chan_mgr_t *m, int chn, nvr_channel_t *out);
int  nvr_chan_list  (nvr_chan_mgr_t *m, nvr_channel_t *out, int cap);

/* ★ 实时向设备取一次型号/序列号/固件版本/厂商(不缓存,每次真取)。
 * NOP/nopOnvif(backend=0)→ getDeviceInfo;ONVIF → GetDeviceInformation(用已存 service_url 直连,不广播)。
 * 供 getChannelInfo:相机升级后靠实时固件版本判成功。含网络往返(数百ms~数秒),调用方应在 hold=0 下调。0=成功。 */
typedef struct { char manufacturer[64]; char model[64]; char firmware[64]; char serial[64]; } nvr_chan_devinfo_t;
int  nvr_chan_query_device_info(nvr_chan_mgr_t *m, int chn, nvr_chan_devinfo_t *out);
nvr_chan_status_t nvr_chan_status(nvr_chan_mgr_t *m, int chn);
const char *nvr_chan_status_name(nvr_chan_status_t s);

/* 子状态（鉴权失败/超解码预算/待激活/休眠/固件升级）：由信号点置位，供 0-7 状态码合成。 */
void nvr_chan_set_substate(nvr_chan_mgr_t *m, int chn, const nvr_chan_substate_t *sub);
/* 仅翻转固件升级子状态(status 码 6)，不覆盖鉴权/休眠等其它标志。 */
void nvr_chan_set_fw_updating(nvr_chan_mgr_t *m, int chn, int on);
/* 取该通道 getChannelStatus 用的 0-7 码：FSM→conn 映射 + 子状态 → nvr_chan_status_code。 */
int  nvr_chan_status_code_of(nvr_chan_mgr_t *m, int chn);

/* 等该通道首次取流尝试结束(出图或失败停等密码)或超时。0=已结束 1=超时。<0 无效。 */
int  nvr_chan_wait_bind(nvr_chan_mgr_t *m, int chn, int timeout_ms);

/* 激活成功后写入账密并落库，清 URL 缓存迫使 ONVIF/8012 用新凭据重连。 */
int  nvr_chan_set_auth(nvr_chan_mgr_t *m, int chn, const char *user, const char *pass);
/** 写入 NOP digest random + P_enh（random 空=普通模式）。同 IP 多源请逐通道调。 */
int  nvr_chan_set_enh(nvr_chan_mgr_t *m, int chn, const char *random, const char *penh);

/* 把一条 NOP 命令 POST 到该通道(0-based)设备的发现口 /APPJsonCmd。
 * kind=NOP：任意 func；kind=nopOnvif：仅 nightowl_protocol 白名单；纯 ONVIF / 无 IP / 失败 → NULL。
 * 未给 args_json 时带设备侧 dev_chn。 */
char *nvr_chan_dev_post(nvr_chan_mgr_t *m, int chn, const char *func, const char *args_json);

/* nightowl_protocol.md 里 nopOnvif 私有 NOP（白灯/警笛/一键报警/激活）。
 * 1=应走发现口 /APPJsonCmd，不要 ONVIF SOAP。通用 ONVIF 返回 0。 */
int nvr_chan_noponvif_priv_func(const char *func);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CHANNEL_H */
