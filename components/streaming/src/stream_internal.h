/* stream_internal.h — streaming 内部共享（puller[C++] ↔ router[C] ↔ mgr[C++]） */
#ifndef STREAM_INTERNAL_H
#define STREAM_INTERNAL_H

#include "nvr_streaming.h"
#include "mhal_vdec.h"        /* platform: 硬解 */
#include "rsdk.h"             /* recorder: 录像 */

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_MAX_CH 16

/* 单通道运行上下文。puller 把它作为 CRtspClient 的 userdata 传回回调。 */
typedef struct stream_chan {
    nvr_stream_chan_cfg_t cfg;
    int              codec;         /* 解析确定的 rsdk codec: 0=H264 1=H265 */
    nvr_ch_state_t   state;

    /* 平台解码器（bind 到 vout_win → 解码即上屏） */
    mhal_vdec_t     *vdec;
    int              decode_denied;  /* 1=因解码预算超限被拒（只录不显）*/
    /* 录像写入器（盘组） */
    rsdk_writer_t   *writer;
    int              rec_gated;     /* 0=还没等到关键帧, 未起录; 1=已起录 */

    void            *puller;        /* CRtspClient*（C 侧不透明） */
    struct nvr_stream_mgr *mgr;
} stream_chan_t;

/* ---- router (stream_router.c, 纯 C) ---- */
rsdk_err_t stream_router_open (stream_chan_t *c, rsdk_group_t *grp);  /* 开 vdec + writer */
void       stream_route_video (stream_chan_t *c, const uint8_t *data, int len,
                               uint32_t ts, uint16_t seq);
void       stream_route_audio (stream_chan_t *c, const uint8_t *data, int len, uint32_t ts);
void       stream_router_close(stream_chan_t *c);

#ifdef __cplusplus
}
#endif
#endif
