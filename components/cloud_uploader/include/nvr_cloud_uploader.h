/***************************************************************************************
 *  nvr_cloud_uploader.h — 云存上传引擎（计划 §B6）
 *
 *  异步(有盘): 轮询 rsdk_cloud → 读盘段 → TS → VSaaS。
 *  同步(无盘): 事件窗内旁路取流 → 内存 TS 分片 → VSaaS + update_tags。
 *
 *  门控：云存开关 ON + stoken 非空 + 联网。开关/stoken/配置由 NOP handler 经设置库写入。
 ***************************************************************************************/
#ifndef NVR_CLOUD_UPLOADER_H
#define NVR_CLOUD_UPLOADER_H

#include "rsdk.h"             /* rsdk_group_t / rsdk_cloud（meta ctx） */

typedef struct nvr_settings nvr_settings_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_cloud_uploader nvr_cloud_uploader_t;

typedef struct {
    rsdk_group_t    *group;          /* 读事件段的盘组；NULL=同步(无盘)模式 */
    void            *meta;           /* rsdk_meta ctx（云存状态权威） */
    nvr_settings_t  *settings;       /* borrowed：cloud_channel.stream_type / triggers */
    const char   *udid;           /* 设备 UID（stream_url 路径） */
    const char   *stoken;         /* 初始 stoken（可后续 set_stoken 更新） */
    int           stage;          /* 0=prod 域名, 1=stage 域名 */
    int           worker_count;   /* 并发 worker 数（默认 2） */
    int           poll_interval_s;/* 轮询待传间隔（默认 5） */
    int           slice_ms;       /* TS 切片时长（默认 15000） */
} nvr_cloud_uploader_cfg_t;

/* ★云存出厂默认(单一宏:uploader.c 回退 + nvr_app 传入 都引用,改一处即统一)。实机可调。 */
#ifndef NVR_CLOUD_DEF_POLL_S
#define NVR_CLOUD_DEF_POLL_S    5      /* 轮询待传间隔秒 */
#endif
#ifndef NVR_CLOUD_DEF_SLICE_MS
#define NVR_CLOUD_DEF_SLICE_MS  15000  /* TS 切片时长毫秒 */
#endif

int  nvr_cloud_uploader_start(const nvr_cloud_uploader_cfg_t *cfg, nvr_cloud_uploader_t **out);
void nvr_cloud_uploader_stop (nvr_cloud_uploader_t *up);

/* NOP handler 门控（经设置库变更通知或直接调用）：开关 / stoken 更新。 */
void nvr_cloud_uploader_set_switch(nvr_cloud_uploader_t *up, int on);
void nvr_cloud_uploader_set_stoken(nvr_cloud_uploader_t *up, const char *stoken);
/* 服务器 -1002/-1003/-1004 → 强制关开关（并使 NOP get* 返回 false，由上层同步设置库）。 */
void nvr_cloud_uploader_force_off (nvr_cloud_uploader_t *up, int reason_code);

/* 有盘装配后切换异步模式（更新 group 指针）。 */
void nvr_cloud_uploader_set_group(nvr_cloud_uploader_t *up, rsdk_group_t *group);
int  nvr_cloud_uploader_sync_mode(const nvr_cloud_uploader_t *up);

/* 同步模式：事件开始/结束（由 app 编排层在 nvr_rec_trigger_event 后调用）。 */
int  nvr_cloud_sync_event_begin(nvr_cloud_uploader_t *up, int chn, uint64_t eid,
                                uint32_t starttime, uint32_t rectype, int rec_stream);
int  nvr_cloud_sync_event_end(nvr_cloud_uploader_t *up, uint64_t eid, uint32_t end_epoch);

/* streaming 旁路喂帧（weak 默认空；cloud_uploader 提供强符号）。 */
void nvr_cloud_sync_feed(int chn, int stream, const uint8_t *data, int len,
                         int codec, int is_key, uint32_t ts_ms);

/* 诊断 */
int  nvr_cloud_uploader_online(nvr_cloud_uploader_t *up);   /* 开关 ON 且 stoken 就绪 */

#ifdef __cplusplus
}
#endif
#endif /* NVR_CLOUD_UPLOADER_H */
