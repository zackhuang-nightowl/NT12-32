/* stream_internal.h — streaming 内部共享（puller[C++] ↔ router[C] ↔ mgr[C++]）
 *
 * ★ 双流架构:每通道**主+子两路常拉**。
 *   · 主码流(pmain):录像(高质量) + 单宫格显示。
 *   · 子码流(psub) :录像 + 多宫格显示。
 *   录像:主+子**两路都录**(同一 writer,按 f.stream 标记)。
 *   显示:单个硬件解码器,由 decode_stream(单宫格=主/多宫格=子)那一路喂;切换只改 decode_stream,
 *        两路都在拉 → **瞬时切换、不重连**。show_win<0 则不解码(门控)。
 */
#ifndef STREAM_INTERNAL_H
#define STREAM_INTERNAL_H

#include "nvr_streaming.h"
#include "mhal_vdec.h"        /* platform: 硬解 */
#include "rsdk.h"             /* recorder: 录像 */

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_MAX_CH 32   /* 整机通道:16 PoE + 16 LAN(与 app nvr_channel.h / mhal MHAL_MAX_CH 一致) */

struct stream_chan;

/* 单路码流(主或子)拉取上下文:各自 RTSP client / codec / 参数集缓存 / 帧计数。 */
typedef struct stream_pull {
    struct stream_chan *owner;      /* 回指所属通道(回调 userdata) */
    int              stream;        /* NVR_STREAM_MAIN(0) / NVR_STREAM_SUB(1) */
    void            *puller;        /* nop::NopRtspClient*（C 侧不透明；NULL=未拉） */
    char             url[256];      /* 该路取流 URL(空=未配置,不拉此路) */
    int              codec;         /* 解析确定的 rsdk codec: 0=H264 1=H265;-1=未定 */
    int              connected;     /* 1=CONNSUCC(codec 有效) */

    /* 参数集缓存:相机把 SPS/PPS(/VPS) 作为独立小帧发,IDR 关键帧不含参数集 → Novatek 硬解组不成
     * 完整 AU(全黑)。缓存参数集,IDR 前拼回去。每路独立。 */
    uint8_t          par[512];
    int              par_len;
    int              par_building;

    /* ★ 最近一个完整关键帧缓存(参数集已拼好)。切到本路解码(开解码器)时立即喂它 →
     * 不等下个 IDR、瞬时出图。每 GOP 覆盖一次;按需 realloc(主码流 8K IDR 可较大)。 */
    uint8_t         *kf;
    int              kf_len;
    int              kf_cap;
    uint32_t         kf_ts;

    /* 诊断:首帧标记 + 累计帧/字节 + 上个 IDR 帧号(测 GOP 间隔) */
    unsigned         vframes;
    unsigned long    vbytes;
    unsigned         last_idr_f;
} stream_pull_t;

/* 单通道运行上下文。 */
typedef struct stream_chan {
    nvr_stream_chan_cfg_t cfg;
    nvr_ch_state_t   state;
    struct nvr_stream_mgr *mgr;

    stream_pull_t    pmain;         /* 主码流路 */
    stream_pull_t    psub;          /* 子码流路 */

    /* 平台解码器(单个;由 decode_stream 那一路喂 → 解码即上屏) */
    mhal_vdec_t     *vdec;
    int              decode_stream; /* 当前喂解码器的码流:NVR_STREAM_MAIN/SUB(单宫格=主,多宫格=子) */
    int              decode_denied; /* 1=解码预算超限被拒(只录不显) */
    int              show_win;      /* 显示目标格:-1=隐藏(只拉+录,不解码);>=0=可见格号 */

    /* 录像写入器(盘组;主+子两路都写此 writer,按 f.stream 区分)。 */
    rsdk_writer_t   *writer;
    int              rec_gated_main;/* 主路录像关键帧门控(从 IDR 起) */
    int              rec_gated_sub; /* 子路录像关键帧门控 */

    rsdk_group_t    *grp;           /* 录像盘组:延迟到就绪后开 router 时用 */
    int              router_open;   /* 1=已开 writer/就绪 */
    int              fed_since_open; /* 开解码后已喂给解码器的帧数(供"出图就绪"判定:切宫格阻塞回复用) */
    int              live_synced;    /* 1=已从一个"实时" IDR 干净起播(其后连续 P 帧参考链有效);
                                        0=尚未 → 只喂关键帧、丢弃 P(避免参考链断裂花屏/卡旧图)。
                                        长 GOP 8K 切回后靠它等到实时 IDR 再干净起播。 */
} stream_chan_t;

/* ---- router (stream_router.c, 纯 C) ---- */
rsdk_err_t stream_router_open (stream_chan_t *c, rsdk_group_t *grp);  /* 开 writer(+可见则解码) */
void       stream_open_writer (stream_chan_t *c, rsdk_group_t *grp);  /* 仅补开 writer(格式化后重组装用) */
void       stream_close_writer(stream_chan_t *c);                    /* 运行时关 writer(录像开关关) */
void       stream_chan_get_dim(stream_chan_t *c, int stream, int *w, int *h, int *fps); /* 回放:取解码尺寸 */
void       stream_decode_open (stream_chan_t *c, int win);           /* 开解码器绑到 win(用 decode_stream 那路的 codec/分辨率) */
void       stream_decode_close(stream_chan_t *c);                    /* 关解码器(隐藏,不动拉流/录像) */
void       stream_feed_keyframe(stream_chan_t *c);                   /* 喂缓存关键帧(解码器须已 start)→ 秒出图 */
/* 一路码流来一帧:录像(标记 p->stream) +（若 p->stream==decode_stream 且可见)喂解码器。 */
void       stream_route_video (stream_pull_t *p, const uint8_t *data, int len,
                               uint32_t ts, uint16_t seq);
void       stream_route_audio (stream_pull_t *p, const uint8_t *data, int len, uint32_t ts);
void       stream_router_close(stream_chan_t *c);

#ifdef __cplusplus
}
#endif
#endif
