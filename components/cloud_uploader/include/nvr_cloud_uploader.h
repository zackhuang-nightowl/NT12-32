/***************************************************************************************
 *  nvr_cloud_uploader.h — 云存异步上传引擎（计划 §B6）
 *
 *  事件切片上传：轮询 recorder 的云存待传队列(rsdk_cloud) → 取事件段(rsdk_group_query)
 *  → 读帧(rsdk_group_play) → TS 封装分片(ts_mux) → GET stream_url → multipart POST
 *  (http_vsaas) → 回写 rsdk_cloud 状态。异步(有盘)策略；同步(无盘)后续补。
 *
 *  门控：云存开关 ON + stoken 非空 + 联网。开关/stoken/配置由 NOP handler 经设置库写入。
 ***************************************************************************************/
#ifndef NVR_CLOUD_UPLOADER_H
#define NVR_CLOUD_UPLOADER_H

#include "rsdk.h"             /* rsdk_group_t / rsdk_cloud（meta ctx） */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_cloud_uploader nvr_cloud_uploader_t;

typedef struct {
    rsdk_group_t *group;          /* 读事件段的盘组 */
    void         *meta;           /* rsdk_meta ctx（云存状态权威） */
    const char   *udid;           /* 设备 UID（stream_url 路径） */
    const char   *stoken;         /* 初始 stoken（可后续 set_stoken 更新） */
    int           stage;          /* 0=prod 域名, 1=stage 域名 */
    int           worker_count;   /* 并发 worker 数（默认 2） */
    int           poll_interval_s;/* 轮询待传间隔（默认 5） */
    int           slice_ms;       /* TS 切片时长（默认 15000） */
} nvr_cloud_uploader_cfg_t;

int  nvr_cloud_uploader_start(const nvr_cloud_uploader_cfg_t *cfg, nvr_cloud_uploader_t **out);
void nvr_cloud_uploader_stop (nvr_cloud_uploader_t *up);

/* NOP handler 门控（经设置库变更通知或直接调用）：开关 / stoken 更新。 */
void nvr_cloud_uploader_set_switch(nvr_cloud_uploader_t *up, int on);
void nvr_cloud_uploader_set_stoken(nvr_cloud_uploader_t *up, const char *stoken);
/* 服务器 -1002/-1003/-1004 → 强制关开关（并使 NOP get* 返回 false，由上层同步设置库）。 */
void nvr_cloud_uploader_force_off (nvr_cloud_uploader_t *up, int reason_code);

/* 诊断 */
int  nvr_cloud_uploader_online(nvr_cloud_uploader_t *up);   /* 开关 ON 且 stoken 就绪 */

#ifdef __cplusplus
}
#endif
#endif /* NVR_CLOUD_UPLOADER_H */
