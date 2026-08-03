#ifndef NVR_CMD_DISPLAY_H
#define NVR_CMD_DISPLAY_H
#include "cJSON.h"
#include "nvr_preview.h"
#include "nvr_chan_persist.h"
#include "nvr_channel.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { nvr_preview_t *pv; nvr_chan_persist_t *persist; nvr_chan_mgr_t *cm; } nvr_display_ctx_t;
char *nvr_cmd_display_handle(const char *func, cJSON *args, const nvr_display_ctx_t *ctx);
#ifdef __cplusplus
}
#endif
#endif
