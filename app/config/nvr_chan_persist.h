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
#ifdef __cplusplus
}
#endif
#endif
