#ifndef NVR_CHAN_PERSIST_H
#define NVR_CHAN_PERSIST_H
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NVR_PERSIST_MAX_CH 32
typedef struct nvr_chan_persist nvr_chan_persist_t;
nvr_chan_persist_t *nvr_chan_persist_open(const char *config_dir);
void  nvr_chan_persist_close(nvr_chan_persist_t *);
int   nvr_chan_persist_get_mapping(nvr_chan_persist_t *, int *out1based, int cap);
int   nvr_chan_persist_set_mapping(nvr_chan_persist_t *, const int *map1based, int n);
cJSON *nvr_chan_persist_get_caps(nvr_chan_persist_t *, int channel1based);
int   nvr_chan_persist_set_caps(nvr_chan_persist_t *, int channel1based, cJSON *caps_obj_owned);
/* 出图:每通道状态值(GUI 开机先按上次绘制)。get 无记录返回 -1。 */
int   nvr_chan_persist_set_status(nvr_chan_persist_t *, int channel1based, int status);
int   nvr_chan_persist_get_status(nvr_chan_persist_t *, int channel1based);
/* 每通道自定义名(setInputChannelName;未设返回默认 "Channel<ch>")。 */
int   nvr_chan_persist_set_name(nvr_chan_persist_t *, int channel1based, const char *name);
int   nvr_chan_persist_get_name(nvr_chan_persist_t *, int channel1based, char *out, int cap);
/* 出图:每通道主/子显示分辨率("WxH")。 */
int   nvr_chan_persist_set_res(nvr_chan_persist_t *, int channel1based, const char *main_res, const char *sub_res);
int   nvr_chan_persist_get_res(nvr_chan_persist_t *, int channel1based, char *main_out, int mcap, char *sub_out, int scap);
#ifdef __cplusplus
}
#endif
#endif
