/* stream_internal.h — streaming 内部共享（puller[C++] ↔ router[C] ↔ mgr[C++]）
 *
 * ★ 双流架构:每通道**主+子两路常拉**。
 *   · 主码流(pmain):录像(高质量) + 单宫格显示。
 *   · 子码流(psub) :录像 + 多宫格显示。
 *   录像:主/子**各一 writer**(独立段索引 slot.stream=0/1);音频挂主流。
 *   显示:单个硬件解码器,由 decode_stream(单宫格=主/多宫格=子)那一路喂;切换只改 decode_stream,
 *        两路都在拉 → **瞬时切换、不重连**。show_win<0 则不解码(门控)。
 */
#ifndef STREAM_INTERNAL_H
#define STREAM_INTERNAL_H

#include "nvr_streaming.h"
#include "mhal_vdec.h"        /* platform: 硬解 */
#include "rsdk.h"             /* recorder: 录像 */
#include "stream_hub.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_MAX_CH 32   /* 整机通道:16 PoE + 16 LAN(与 app nvr_channel.h / mhal MHAL_MAX_CH 一致) */

/* 预录环一帧(拥有编码码流拷贝);仅事件待命模式用。主/子各一环。 */
#define NVR_PRE_FPS_ASSUME   15    /* 真实帧率未知(刚连、还没估出)时的兜底 fps */
#define NVR_PRE_FPS_MAX      30    /* 取容上限假设的最大帧率 */
#define NVR_PRE_SEC_MAX      30    /* 预录最长秒数(取容上限) */
#define NVR_PRE_FRAMES_MAX   (NVR_PRE_SEC_MAX * NVR_PRE_FPS_MAX)   /* =900 帧封顶(防 fps 误估爆内存) */
typedef struct {
    uint8_t  *data;
    uint32_t  len;
    uint32_t  ts;
    uint32_t  wall_time;   /* 采集时刻(秒); 预录 flush 到录像队列时透传 */
    uint8_t   is_key;
    uint8_t   codec;
    uint8_t   frame_type;
} stream_pre_frame_t;

struct stream_chan;

/* 单路码流(主或子)拉取上下文:各自 RTSP client / codec / 参数集缓存 / 帧计数。 */
typedef struct stream_pull {
    struct stream_chan *owner;      /* 回指所属通道(回调 userdata) */
    int              stream;        /* NVR_STREAM_MAIN(0) / NVR_STREAM_SUB(1) */
    void            *puller;        /* nop::NopRtspClient*（C 侧不透明；NULL=未拉） */
    char             url[256];      /* 该路取流 URL(空=未配置,不拉此路) */
    int              codec;         /* 解析确定的 rsdk codec: 0=H264 1=H265;-1=未定 */
    int              connected;     /* 1=CONNSUCC(codec 有效) */

    /* ★ 本路**真实编码分辨率**(从 SPS 解析回填,含裁剪窗修正)。0=未知。
     * 硬解/vout 必须按此开(而非猜 720p/8K 默认 → 开错尺寸解不动/越界)。每路独立(主/子不同)。 */
    int              enc_w, enc_h;
    unsigned         enc_probe_frames;  /* 已尝试解析 SPS 的帧数(宽限兜底,避免永不出图) */

    /* 参数集缓存:相机把 SPS/PPS(/VPS) 作为独立小帧发,IDR 关键帧不含参数集 → Novatek 硬解组不成
     * 完整 AU(全黑)。缓存参数集,IDR 前拼回去。每路独立。
     * ★ 2048B:High profile 带 VUI 的 SPS、或 H.265 VPS+SPS+PPS 常 >512B,过小会**截断参数集**→
     *   bootstrap 的 SPS 残缺 → VPU "scan first header error" / profile_idc 误解析。超限告警不静默截断。 */
    uint8_t          par[2048];
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

    /* 实时帧率估计(滚动 ~2s 窗):预录环按真实帧率取容,不再按固定 15fps 假设。 */
    uint64_t         fps_win_ms;      /* 估计窗起点(CLOCK_MONOTONIC ms);0=未起 */
    unsigned         fps_win_frames;  /* 窗内视频帧数 */
    int              fps_est;         /* 估得的真实 fps;0=未估出(用 cfg.fps/假设兜底) */

    /* 码流连续性兜底(TCP 下少见;重连/相机跳帧仍可能断参考 → VPU GAPS_DROP)。
     * last_rtp_seq=上一完整 AU 末包 seq; h264_log2/fn 用于 frame_num 跳变检测。 */
    uint16_t         last_rtp_seq;
    int              rtp_seq_valid;
    int              h264_log2_fn;      /* 0=未从 SPS 学到;合法 4..16 */
    int              h264_prev_fn;      /* -1=无 */
    unsigned         rtp_gap_cnt;       /* 累计疑似 RTP 缺口次数(串口诊断) */
    unsigned         fn_gap_cnt;        /* 累计 frame_num 跳变次数 */
    int              disc_mark;         /* 1=本路检测到不连续(仅标记;live 自消化,不挡录像) */
    unsigned         conn_gen;          /* RTSP 连接代数;重连++ */

    /* Recorder 状态机 + 异步写盘队列(解耦磁盘抖动与 on_video) */
    stream_rec_state_t rec_state;
    unsigned         rec_last_gen;
    volatile int     rec_gap_pending;   /* puller 置位; worker 写 gap 标记后清 */
    stream_record_q_t rec_q;
    int              rec_drop_until_key; /* 1=队列曾满, 正按 GOP 丢弃到下个关键帧(避免写半截 GOP) */
    uint32_t         rec_seg_start_wall; /* 当前录像段起始采集时刻(秒); worker 用于定时切片(IDR 对齐) */

    /* 事件预录环(本路独占;主/子 puller 各写各的,无跨线程争用) */
    stream_pre_frame_t *pre_frames;
    int              pre_cap;
    int              pre_count;
    int              pre_head;
    int              pre_flushed;   /* 本事件片段是否已 flush 本路预录 */
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
    int              vdec_stream;   /* 当前已开解码器**实际所用**的码流(=开解码那刻的 decode_stream)。
                                      * 与 decode_stream 比:相等=仅窗口变→挪窗(rebind);不等=码流/分辨率变→须重开。 */
    int              vdec_win;      /* 当前解码器已绑定的显示格(挪窗比对用;-1=未绑) */
    int              decode_denied; /* 1=解码预算超限被拒(只录不显) */
    int              show_win;      /* 显示目标格:-1=隐藏(只拉+录,不解码);>=0=可见格号 */
    volatile int     decode_dirty;  /* 命令/preview 线程置1:解码状态(show_win/decode_stream)变了,
                                      * 由 puller 线程在自己线程内 open/close 解码器(避免与 mhal_vdec_send
                                      * 并发 → use-after-free 野 chn)。与 writer 的 pend_event 同模式。 */
    volatile int     vout_rebind;   /* 命令/preview 线程置1:强制 puller 重发 mhal_vout_bind(show_win),
                                      * 即使 vdec_win==show_win。用于自由窗(bind_rect)↔宫格切换后 HAL 窗
                                      * 被 unbind(visible=0)但流层 vdec_win 仍停在同格号 → 相等守卫短路
                                      * 不重绑的 desync。见 nvr_preview_set_mode 回宫格清自由窗处。 */

    /* 录像:主/子各一 writer(独立段;slot.stream 区分)。音频写主流。 */
    rsdk_writer_t   *writer_main;
    rsdk_writer_t   *writer_sub;
    int              rec_main_on;       /* 1=录主流(读 record_config.stream_type) */
    int              rec_sub_on;        /* 1=录子流 */
    volatile int     rec_main_close_pend;
    volatile int     rec_sub_close_pend;
    int              rec_gated_main;/* 主路录像关键帧门控(从 IDR 起) */
    int              rec_gated_sub; /* 子路录像关键帧门控 */

    /* 仅事件待命(连续 record=0 时):主/子各自预录环 + 触发后双轨写盘。 */
    int              event_arm;         /* 1=待命预录 */
    int              pre_record_s;      /* 预录秒数 */
    volatile int     event_clip;        /* 1=事件片段进行中(puller 写盘) */

    rsdk_group_t    *grp;           /* 录像盘组:延迟到就绪后开 router 时用 */
    /* 事件标记(命令/事件线程置,puller 线程 owns writer 时应用,避免并发写 writer):
     *   pend_event_id != applied_event_id → puller 调 set_event + mark_event 一次;0=清标签。 */
    volatile uint64_t pend_event_id;    /* 待应用的事件 id(0=清除) */
    uint64_t          applied_event_id; /* 已应用到 writer 的事件 id */
    uint8_t           pend_event_rectype;
    uint8_t           pend_event_cloud;   /* 1=云存事件:建槽后置事件槽 state=PENDING(盘上权威) */
    uint32_t          pend_event_start, pend_event_end;
    uint32_t          applied_event_end;  /* 已应用的窗口止(变化=续录延长→重打 mark_event 延事件槽 end_time) */
    volatile uint64_t pend_event_close_id;   /* 待闭合事件 id(0=无);worker 调 rsdk_rec_end_event 写真实 end */
    volatile uint32_t pend_event_close_time; /* 待闭合事件真实结束墙钟 */
    int              router_open;   /* 1=已开 writer/就绪 */
    int              fed_since_open; /* 开解码后已喂给解码器的帧数(供"出图就绪"判定:切宫格阻塞回复用) */
    int              bootstrap_pending; /* 1=open 时 commit 未就绪未喂 kf;commit 后在 puller 补喂缓存 IDR */
    unsigned         vdec_commit_gen;   /* 本路解码器上次(重)灌关键帧时的 mhal_vout commit 代数。
                                         * 与 mhal_vout_commit_gen() 不一致 = 本路被某次 commit 全停全起重启过
                                         * → puller 重灌缓存 IDR 秒 repaint(消除刷屏/加通道后别路变黑)。 */
    stream_live_state_t live_state; /* Live 状态机(替代 live_synced bool) */
    stream_live_q_t  live_q;        /* 仅 decode 路入队;满丢旧追最新 */
    unsigned         live_gen;      /* 已对齐的 pull conn_gen;不一致则 RESYNC */
    int              live_busy_cnt; /* SYNCED 连续送不进(解码器 FIFO 满)计数:超阈值判"跟不上"→
                                     * 丢帧追最新关键帧(限时延)。成功送入即清 0。 */
    uint32_t         live_stall_since; /* 解码器"有帧但送不进"起始时刻(mono ms;0=未卡)。仅在**有数据却
                                        * 持续送失败**时累计——网络无帧(队列空)不计,正常追帧一送成功即清 0。
                                        * 超 STREAM_LIVE_STALL_MS 判定解码器卡死 → 置 live_rebuild 重建。 */
    volatile int     live_rebuild;   /* 看门狗置1:解码器卡死(RESYNC 也救不回)→ 由 puller 在 decode_dirty 处
                                        * 完整 close+reopen 重建路径。是追帧 RESYNC 之上的升级层,不影响正常追帧。 */
} stream_chan_t;

/* stream_mgr 完整定义(record worker 需遍历通道) */
struct nvr_stream_mgr {
    nvr_stream_mgr_cfg_t cfg;
    stream_chan_t        ch[NVR_MAX_CH];
    int                  used[NVR_MAX_CH];
    int                  active[NVR_MAX_CH];
    uint32_t             rec_mask_last;
    void               (*lp_poke)(void *user);
    void                *lp_poke_user;
};

/* ---- router (stream_router.c, 纯 C) ---- */
rsdk_err_t stream_router_open (stream_chan_t *c, rsdk_group_t *grp);  /* 开 writer(+可见则解码) */
void       stream_open_writer (stream_chan_t *c, rsdk_group_t *grp);  /* 仅补开 writer(格式化后重组装用) */
void       stream_close_writer(stream_chan_t *c);                    /* 运行时关 writer(录像开关关) */
void       stream_close_writer_noflush(stream_chan_t *c);            /* worker 内关 writer(已 drain) */
void       stream_rec_mask_poke(stream_chan_t *c);                   /* 录像位图变化 → longPolling */
void       stream_chan_get_dim(stream_chan_t *c, int stream, int *w, int *h, int *fps); /* 回放:取解码尺寸 */
void       stream_decode_open (stream_chan_t *c, int win);           /* 开解码器绑到 win(用 decode_stream 那路的 codec/分辨率) */
void       stream_decode_close(stream_chan_t *c);                    /* 关解码器(隐藏,不动拉流/录像) */
void       stream_feed_keyframe(stream_chan_t *c);                   /* 喂缓存关键帧(解码器须已 start)→ 秒出图 */
void       stream_live_signal_resync(stream_chan_t *c, stream_pull_t *p, const char *why);
/* 一路码流来一帧:录像(标记 p->stream) +（若 p->stream==decode_stream 且可见)喂解码器。 */
void       stream_route_video (stream_pull_t *p, const uint8_t *data, int len,
                               uint32_t ts, uint16_t seq);
void       stream_route_audio (stream_pull_t *p, const uint8_t *data, int len, uint32_t ts);
void       stream_router_close(stream_chan_t *c);

#ifdef __cplusplus
}
#endif
#endif
