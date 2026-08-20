#ifndef UPLOADER_INTERNAL_H
#define UPLOADER_INTERNAL_H

#include "nvr_cloud_uploader.h"
#include "ts_mux.h"
#include <pthread.h>
#include <stdint.h>

#define SYNC_MAX_SESS 8
#define SYNC_FRAME_Q  96
#define SYNC_FRAME_MAX (256u * 1024u)

typedef struct {
    uint8_t *data;
    int      len;
    uint64_t pts90k;
    int      is_key;
    int      codec;
} sync_frame_t;

typedef struct sync_sess {
    int           active;
    int           ending;
    uint64_t      event_id;
    int           chn;
    int           stream;
    uint32_t      starttime;
    uint32_t      rectype;
    char          tags[128];
    char          url[1024];
    int           url_ok;
    uint32_t      orig_last;
    uint32_t      st_embed;
    uint64_t      base_ms;
    ts_mux_t     *mux;
    uint64_t      slice_base_pts;
    uint64_t      first_pts;
    uint64_t      last_pts;
    int           have_first;
    int           h265;
    uint32_t      end_epoch;
    sync_frame_t  fq[SYNC_FRAME_Q];
    int           fq_head, fq_tail, fq_n;
} sync_sess_t;

struct nvr_cloud_uploader {
    nvr_cloud_uploader_cfg_t cfg;
    char      stoken[256];
    int       sw_on;
    volatile int running;
    pthread_t thr;
    pthread_mutex_t lk;
    sync_sess_t sync[SYNC_MAX_SESS];
};

#endif /* UPLOADER_INTERNAL_H */
