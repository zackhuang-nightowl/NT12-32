/***************************************************************************************
 *  nvr_chan_nop_sync.h — nvr_channel_t ↔ nop_nvr_channels 注册表同步（ONVIF 映射用）
 ***************************************************************************************/
#ifndef NVR_CHAN_NOP_SYNC_H
#define NVR_CHAN_NOP_SYNC_H

#include "nvr_config.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nop_sdk/nop_onvif_map.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nvr_chan_mgr;

/** nvr_channel_t → nop_nvr_channel_entry_t（backend/kind 与分类器一致）。 */
void nvr_chan_to_nop_entry(const nvr_channel_t *d, nop_nvr_channel_entry_t *e);

/** 写入/更新一条通道；IP/凭证/backend 变更时刷新 ONVIF 映射会话。 */
void nvr_chan_nop_sync_upsert(nop_nvr_channels_t *reg, const nvr_channel_t *d,
                              nop_onvif_map_backend_t *onvif_be);

/** 删除一条通道注册并失效对应 ONVIF 会话。 */
void nvr_chan_nop_sync_remove(nop_nvr_channels_t *reg, int chn,
                              nop_onvif_map_backend_t *onvif_be);

/** 从通道管理器全量刷新注册表（启动时；不逐条 invalidate，backend 刚创建）。 */
void nvr_chan_nop_sync_all(nop_nvr_channels_t *reg, struct nvr_chan_mgr *cm);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CHAN_NOP_SYNC_H */
