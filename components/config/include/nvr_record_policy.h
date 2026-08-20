/***************************************************************************************
 *  nvr_record_policy.h — 录像/云存码流策略（读 nvr_settings，驱动 streaming / 云存）
 ***************************************************************************************/
#ifndef NVR_RECORD_POLICY_H
#define NVR_RECORD_POLICY_H

#include "nvr_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* stream_type: "both" | "main" | "sub" | "disable"(本地可全关) */
void nvr_record_stream_mask(const char *stream_type, int *main_on, int *sub_on);

/* triggers CSV 是否含 trigger(精确 token,逗号分隔) */
int nvr_triggers_csv_has(const char *csv, const char *trigger);

/* 云存上传/登记门控:cloud.switch + enable + stream_type + triggers。
 * 允许时 *out_stream:0=主 1=子;返回 1=允许,0=跳过。 */
int nvr_cloud_ch_upload_stream(nvr_settings_t *s, int chn0, const char *trigger, int *out_stream);

/* 异步(有盘)云存：是否上传 start_epoch 时刻的事件。
 * cloud.async_upload_since 空=全部上传；非空 UTC 秒=仅该时刻及之后。 */
int nvr_cloud_async_upload_allowed(nvr_settings_t *s, uint32_t start_epoch);

#ifdef __cplusplus
}
#endif
#endif /* NVR_RECORD_POLICY_H */
