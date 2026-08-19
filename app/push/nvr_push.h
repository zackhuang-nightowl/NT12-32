/***************************************************************************************
 *  nvr_push.h — 事件推送引擎（配置已在 SQLite；本模块只做策略 + 读事件图 + 上传 + TPNS）。
 *
 *  图：rsdk_pic（事件抓拍已落盘），不另截。上传失败仍发文字推送。
 ***************************************************************************************/
#ifndef NVR_PUSH_H
#define NVR_PUSH_H

#include "nop_sdk/nop_detect_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_push nvr_push_t;

typedef struct {
    struct nvr_settings     *settings;
    struct nvr_chan_persist *persist;
    struct rsdk_group       *group;
    void                    *meta;     /* rsdk_meta；可 NULL=不带图 */
} nvr_push_opt_t;

int  nvr_push_start(const nvr_push_opt_t *opt, nvr_push_t **out);
void nvr_push_stop (nvr_push_t *p);

/* 事件线程：只入队。chn 0-based。 */
void nvr_push_on_event(nvr_push_t *p, int chn, uint64_t eid, uint32_t ts,
                       nop_detect_type_t type);

#ifdef __cplusplus
}
#endif
#endif /* NVR_PUSH_H */
