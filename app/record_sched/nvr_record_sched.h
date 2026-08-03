/***************************************************************************************
 *  nvr_record_sched.h — 录像调度/事件与云存协调（app 集成层，计划 §B3）
 *
 *  设计边界（重要）：**连续录像由 ③streaming 负责**（通道 record=1，帧路径直写 recorder）。
 *  本模块不重复开连续 writer，职责为：
 *    - 事件与云存协调：AI 触发 → 在连续录像轨上按时窗登记「云存事件」(rsdk_cloud)，
 *      供上传器择机上传（R-event 退路：对连续轨按事件时窗取片）。
 *    - 满盘策略编排（STOP：停录；OVERWRITE：recorder 内部回收）。
 *    - 每通道录像状态（供预览 REC 图标 / NOP 查询）。
 ***************************************************************************************/
#ifndef NVR_RECORD_SCHED_H
#define NVR_RECORD_SCHED_H

#include "rsdk.h"             /* rsdk_group_t / rectype / rsdk_cloud */
#include "nvr_storage.h"      /* nvr_stg_evt_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_rec_sched nvr_rec_sched_t;

typedef struct {
    rsdk_group_t *group;          /* borrowed：盘组（判断有无录像）；NULL=不录像 */
    void         *meta;           /* rsdk_meta ctx（云存事件登记）；可 NULL */
    int           hdd_full_policy;/* RSDK_HDDFULL_OVERWRITE / _STOP */
    int           post_record_s;  /* 事件时窗后录秒（默认 10） */
} nvr_rec_sched_cfg_t;

int  nvr_rec_sched_init  (const nvr_rec_sched_cfg_t *cfg, nvr_rec_sched_t **out);
void nvr_rec_sched_deinit(nvr_rec_sched_t *r);

/* 通道上/下线（channel_mgr 回调驱动）：登记录像状态（连续录像本身由 streaming 起停）。 */
int  nvr_rec_channel_up  (nvr_rec_sched_t *r, int chn, int codec);
int  nvr_rec_channel_down(nvr_rec_sched_t *r, int chn);

/* AI 事件触发：按时窗登记/延长云存事件（连续轨）。返回 event_id（0=未登记）。 */
uint64_t nvr_rec_trigger_event(nvr_rec_sched_t *r, int chn, int rectype, uint32_t start_epoch);

/* 满盘/盘事件（app on_storage_evt 转发）：STOP 策略下置全局停录标志。 */
void nvr_rec_on_storage_evt(nvr_rec_sched_t *r, nvr_stg_evt_t e);

/* 周期：结束到期事件时窗（写 end_time / 供上传器判定完整）。 */
void nvr_rec_tick(nvr_rec_sched_t *r);

int  nvr_rec_is_recording(nvr_rec_sched_t *r, int chn);
int  nvr_rec_stopped(nvr_rec_sched_t *r);   /* 满盘 STOP 是否生效 */

#ifdef __cplusplus
}
#endif
#endif /* NVR_RECORD_SCHED_H */
