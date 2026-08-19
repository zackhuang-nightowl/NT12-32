/***************************************************************************************
 *  nvr_config.h — 配置加载：JSON(config 目录下 .json) → 各模块运行期结构
 *
 *  核心：把 channels.json 的 device→source 扁平化成每通道参数（喂 ③streaming/⑤recorder），
 *  统一处理 PoE 通道 / 数字通道 / 单设备多视频源 / 路数可配。见 config/README.md。
 ***************************************************************************************/
#ifndef NVR_CONFIG_H
#define NVR_CONFIG_H

#include "nvr_streaming.h"     /* nvr_stream_mgr_cfg_t / codec·stream 枚举 */
#include "nvr_storage.h"       /* nvr_storage_cfg_t */
#include "nvr_settings.h"      /* 运行期可写配置库（overlay 源） */

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_CFG_MAX_CH 64

/* 一个通道（device→source 扁平化后的结果，映射 nvr_stream_chan_cfg_t） */
typedef struct {
    int  chn;
    char name[64];
    char url[256];         /* 空 + onvif_auto=1 → 待 ONVIF GetStreamUri 填 */
    char user[64];
    char pass[64];
    int  codec;            /* NVR_CODEC_AUTO/H264/H265 */
    int  stream;           /* NVR_STREAM_MAIN/SUB */
    int  record;
    int  vout_win;         /* 默认 = chn */
    int  onvif_auto;       /* 1=经 ONVIF 取流 URL */
    char onvif_ip[64];
    int  onvif_port;
    int  poe_port;         /* >0=PoE 口; 0=数字通道 */
    int  kind;             /* 设备子类型 0=NOP 1=NOPONVIF 2=ONVIF（nvr_dev_kind_t） */
    int  backend;          /* 控制面后端 0=NOP 透传 1=ONVIF 翻译（nvr_backend_t） */
    int  enabled;          /* 0=禁用(跳过) 1=启用 */
    char mac[24];          /* 设备 MAC（发现分类得；IP 变更后按 mac 找回；持久化写 camera） */
    char model[48];        /* 型号：ONVIF discovery scopes 的 /hardware/（发现/添加时填，持久化） */
    char serial[64];       /* 设备 SN：scopes /serial/ 或 /sn/（nopOnvif 激活用） */
    char service_url[128]; /* 发现得到的 device_service 路径 */
    /* --- 多视频源(Multi-VideoSource):一物理设备 N 源各占一 channel,按 videoSourceToken 区分 --- */
    char video_source_token[100]; /* 该 channel 绑定的 ONVIF VideoSourceToken(空=单源/NOP,零回归) */
    int  dev_chn;                 /* 源序号(设备侧 channel;单源=1;透传改写目标) */
    char type[8];                 /* "single" | "multi"(多源;同 mac 归组) */
    char enh_random[32];          /* NOP digest random；空=普通模式。NVR reset 才清。 */
} nvr_channel_t;

/* 系统级 */
typedef struct {
    char model[32], name[64], sn[64];
    int  capacity, poe_ports, ip_channels;
    int  hdmi_w, hdmi_h, default_layout;
} nvr_sys_cfg_t;

/* 聚合配置 */
typedef struct {
    nvr_sys_cfg_t        sys;
    nvr_storage_cfg_t    storage;    /* 数据字段由 loader 填；cb/device_sn 由 app 设 */
    nvr_stream_mgr_cfg_t stream;     /* 超时等；group 由 app 装配后填 */
    nvr_channel_t        ch[NVR_CFG_MAX_CH];
    int                  nch;
} nvr_config_t;

/* 从 dir 下加载 system/storage/streaming/channels.json；返回 0 成功。 */
int nvr_config_load(const char *dir, nvr_config_t *out);

/* 用运行期设置库覆盖只读 JSON 结果（计划 §B7 桥接）：
 *  - channel 表：叠加/新增用户添加的通道（enabled=0 跳过），应用 name/url/creds/kind 覆盖；
 *  - 标量：device_name / storage 策略等。
 * settings 为 NULL 时原样返回。返回 0 成功。 */
int nvr_config_overlay_from_settings(nvr_config_t *cfg, nvr_settings_t *settings);

#ifdef __cplusplus
}
#endif
#endif
