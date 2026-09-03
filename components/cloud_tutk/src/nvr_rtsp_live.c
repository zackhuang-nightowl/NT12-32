/***************************************************************************************
 *  nvr_rtsp_live.c — 隧道 RTSP 直播服务(基于 SDK 标准 nop::NopRtspServer)
 *
 *  历史:本文件曾内含一套手搓 RTSP/RTP 服务端(accept 线程 + 逐 NAL 打包 + 自维护
 *  时戳)。因手搓服务端**没有媒体时钟**,喂帧节奏由 puller 决定,隧道侧出现"发得比
 *  放得快 → 画面冻结"的老 bug。现改为委托给 OnvifClientLibrary 的 nop::NopRtspServer:
 *  它对每个视频帧用 get_rtp_timestamp(90000)(挂钟 90kHz)打时戳并作为节拍器——
 *  服务端即节拍器,从根本上修掉该问题。
 *
 *  本文件现在只负责:
 *    - 32 路 slot 表:把 (chn,stream) 映射到 NopRtspServer 的 slot(URL "live{N}")
 *    - 首个关键帧到达时 addStream(注册编码/维度),随后每帧 pushVideo(不重新打包)
 *    - 主流音频 pushAudio
 *  RTP/RTSP 协议栈、SDP(sprop 参数集由 getAuxSDPLine 从关键帧自动生成)全部由 SDK 负责。
 *
 *  公开 C API(nvr_rtsp_live.h)保持逐字节不变:stream_router.c / nvr_cmd_p2p.c 依赖它。
 *  远程回放(pb_*):nvr_rtsp_pb_prepare 登记 + 查时长,并启动每通道"读盘线程"——从
 *  RSDK 段顺序取帧,按帧 pts(90kHz)增量实时节拍 pushVideo(slot==通道号),复用与直播
 *  同一 NopRtspServer/URL 方案(URL 按文档 /playback/<startTime>);slot 标 pb_mode,直播
 *  喂帧跳过该 slot。回放 RTP 时戳=帧真实媒体时间(pushVideo 带 ts)→ 时间轴反映录像时间。
 *  seek=RTSP SET_PARAMETER(playback_ctrl:seek + year/月/日/时/分/秒 UTC)→ 读线程重定位+
 *  暂停,收到 play(SET_PARAMETER playback_ctrl:play 或 PLAY 方法)恢复。音频 v1 暂不出。
 ***************************************************************************************/
#define _GNU_SOURCE
#include "nvr_rtsp_live.h"
#include "nvr_rtsp_srv.h"
#include "rsdk.h"
#include "nvr_log.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NVR_SRV_MAX_SLOTS   32
#define NVR_SRV_ACODEC_AAC   4    /* == nop::NOP_AUDIO_CODEC_AAC(C 侧不便包含 C++ 头) */
#define NVR_AAC_NBSAMPLES 1024    /* 一帧 AAC 采样数 */
#define PB_MAX_SEGS        512

/* slot 表项:used=占用;chn/stream=映射键;inited=已 addStream;has_audio=注册了音频轨。
 * 回放(Task 5):pb_mode=1 表示该 slot 被"远程回放读盘线程"占用 —— 直播喂帧
 * (nvr_rtsp_live_feed)必须跳过这类 slot,让回放独占该通道的码流(直播/回放对同一
 * 通道互斥,见 controller amendment)。回放读线程按帧 pts 增量实时节拍推流。 */
typedef struct {
    int used;
    int chn;
    int stream;
    int inited;
    int has_audio;
    /* ---- 回放占用(Task 5) ---- */
    int          pb_mode;        /* 1=本 slot 由回放读线程占用 */
    pthread_t    pb_tid;         /* 读线程句柄(pb_mode 时有效) */
    volatile int pb_run;         /* 停止标志:置 0 让读线程退出 */
    uint32_t     pb_start;       /* 回放起点 UTC 秒 */
    uint32_t     pb_end;         /* 事件回放止点 UTC 秒(0=不限,时间轴回放) */
    int          pb_stream;      /* 回放码流 0主/1子 */
    int          pb_want_audio;  /* 音频意向(v1 暂不出音,见报告) */
    int          pb_want_video;  /* 视频意向(恒 1) */
    /* ---- SET_PARAMETER seek(文档:playback_ctrl:seek/play) ---- */
    volatile uint32_t pb_seek_utc;   /* seek 目标 UTC 秒 */
    volatile int      pb_seek_req;   /* 1=读线程应重定位到 pb_seek_utc */
    volatile int      pb_paused;     /* 1=读线程暂停推流(seek 后等 play) */
} live_slot_t;

static struct {
    volatile int       run;
    int                port;
    pthread_mutex_t    lock;
    struct rsdk_group *group;
    live_slot_t        slot[NVR_SRV_MAX_SLOTS];
    /* 每通道最近一次喂帧的 codec(0=H264 1=H265,-1=未知)。录像喂流常开,故所有
     * 在拉的通道都会被记录,startLiveStream 据此选 .264/.265 扩展名。不占服务资源。 */
    int                chan_codec[NVR_SRV_MAX_SLOTS];
    /* 远程回放登记(Task 5 出流用),环形 8 条;pb_prepare 写,lookup 读。 */
    struct {
        uint32_t start;
        int      chn;
        int      stream;
        int      want_audio;
        int      want_video;
        int      used;
    } pb_bind[8];
    int                pb_bind_i;
} g;

/* ---------------- slot 池(调用方须持 g.lock) ---------------- */

/* slot 索引 == 通道号(每通道一个 slot;同一时刻只服务该通道的一条码流)。URL
 * ch<N>_<S> 因此自描述 slot=N,库侧 rtsp_parse_live_url 直接映射,无需回调查表。 */
static int slot_of_locked(int chn, int stream)
{
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    if (g.slot[chn].used && g.slot[chn].stream == stream)
        return chn;
    return -1;
}

static void free_slot_locked(int slot);   /* fwd */

static int alloc_slot_locked(int chn, int stream, int want_v, int want_a)
{
    (void)want_v;   /* 视频恒开;保留形参以对齐设计接口 */
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    /* 该通道正被回放独占 → 直播不得抢占(直播/回放对同一通道互斥)。安全拒绝,由调用方降级。 */
    if (g.slot[chn].pb_mode) return -1;
    /* 该通道已占但码流不同(主↔子切换)→ 先释放旧 slot 再按新码流重注册。 */
    if (g.slot[chn].used && g.slot[chn].stream != (stream ? 1 : 0))
        free_slot_locked(chn);
    g.slot[chn].used      = 1;
    g.slot[chn].chn       = chn;
    g.slot[chn].stream    = stream ? 1 : 0;
    g.slot[chn].has_audio = want_a ? 1 : 0;
    /* inited 由 free_slot_locked 清零(切换时);未占用时本就是 0。 */
    return chn;
}

static void free_slot_locked(int slot)
{
    if (slot < 0 || slot >= NVR_SRV_MAX_SLOTS) return;
    if (g.slot[slot].used)
        nvr_srv_free_stream(slot);
    memset(&g.slot[slot], 0, sizeof(g.slot[slot]));
}

/* ---------------- 回放出流(Task 5) ---------------- */

/* 重叠段剩余秒;无段/无盘=0(不写死)。 */
static uint32_t pb_duration_of(struct rsdk_group *grp, int chn, uint32_t start)
{
    uint32_t dur = 0;
    if (!grp) return 0;
    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(*segs) * 64);
    if (!segs) return 0;
    int n = rsdk_group_query(grp, start, start + 3600, chn, -1, segs, 64);
    for (int i = 0; i < n; i++) {
        uint32_t t0 = segs[i].start_time;
        uint32_t t1 = segs[i].end_time;
        if (t1 == 0 || t1 == 0xFFFFFFFFu) continue;
        if (t0 <= start && start < t1) {
            dur = t1 - start;
            break;
        }
    }
    free(segs);
    return dur;
}

/* 事件回放:按 fileName(=事件起点 UTC,即 evtidx 槽 start_time,含预录)在盘上事件索引区
 * 查该通道的事件,返回事件时长(end-start),并把止点写 *end_out。找不到=0(调用方回落连续段)。 */
static uint32_t pb_event_window_of(struct rsdk_group *grp, int chn, uint32_t start, uint32_t *end_out)
{
    if (end_out) *end_out = 0;
    if (!grp) return 0;
    rsdk_evt_slot_t *slots = (rsdk_evt_slot_t *)calloc(64, sizeof(*slots));
    if (!slots) return 0;
    uint32_t dur = 0;
    for (int di = 0; di < rsdk_group_count(grp) && !dur; di++) {
        rsdk_dev_t *d = rsdk_group_dev(grp, di);
        if (!d) continue;
        /* 窗口取 [start, start+1]:fileName 恰为事件槽 start_time,精确命中即可。 */
        int m = rsdk_evtidx_query(d, start, start + 1, chn, -1, slots, 64, 1);
        for (int i = 0; i < m; i++) {
            if ((int)slots[i].chn != chn || slots[i].start_time != start) continue;
            uint32_t end = slots[i].end_time;
            if (end == 0 || end == 0xFFFFFFFFu || end <= start)
                end = start + 30u;  /* 未闭合/异常 → 兜底 30s(出厂后录窗量级) */
            if (end_out) *end_out = end;
            dur = end - start;
            break;
        }
    }
    free(slots);
    return dur;
}

/* 按起点/最近一次 startPlayback 找回登记的通道/码流/音视频意向。Task 5 出流时使用。 */
__attribute__((unused))
static void lookup_pb_bind(uint32_t start, int *chn, int *stream, int *want_audio, int *want_video)
{
    for (int i = 0; i < 8; i++) {
        if (g.pb_bind[i].used && g.pb_bind[i].start == start) {
            *chn = g.pb_bind[i].chn;
            *stream = g.pb_bind[i].stream;
            if (want_audio) *want_audio = g.pb_bind[i].want_audio;
            if (want_video) *want_video = g.pb_bind[i].want_video;
            return;
        }
    }
    int last = (g.pb_bind_i + 7) % 8;
    if (g.pb_bind[last].used) {
        *chn = g.pb_bind[last].chn;
        *stream = g.pb_bind[last].stream;
        if (want_audio) *want_audio = g.pb_bind[last].want_audio;
        if (want_video) *want_video = g.pb_bind[last].want_video;
    }
}

/* 回放读盘线程:顺序取该通道的录像段,按帧 pts 增量实时节拍 pushVideo。
 * NopRtspServer 用挂钟给每帧打 90kHz 时戳(pushVideo 无时戳),故 1× 播放靠"按真实
 * 时间间隔推送"实现——本线程负责节拍,而不是让服务端猜。
 *
 * pts 单位:90kHz(读 app/playback/nvr_playback.c 与 stream_record_worker.c 确认:
 * f.pts = 相机 RTP ts;回放判据用 (pts-prev)/90 得毫秒,且 <90000*2 视作 <2s)。故
 * 帧间隔毫秒 = (pts[n]-pts[n-1]) / 90,并夹到 [0,500] ——pts 回绕/跨段跳变永不冻结/快进。 */
/* 从 utc 起打开一天内该通道的连续录像段播放器;无段返回 NULL(不算错误,读线程 hold)。 */
static rsdk_group_player_t *pb_open_at(struct rsdk_group *grp, int chn, int pbstream,
                                       uint32_t utc, rsdk_index_slot_t *segs)
{
    int nseg = rsdk_group_query_stream(grp, utc, utc + 24u * 3600u, chn,
                                       RSDK_REC_CONTINUOUS, pbstream, segs, PB_MAX_SEGS);
    if (nseg <= 0) {
        NVR_LOGD("playback", "pb ch%d st%d @%u: 无录像段(hold)", chn, pbstream, utc);
        return NULL;
    }
    rsdk_group_player_t *pl = NULL;
    if (rsdk_group_play_open(grp, segs, nseg, &pl) != RSDK_OK || !pl) {
        NVR_LOGW("rtsp", "pb ch%d: play_open 失败", chn);
        return NULL;
    }
    return pl;
}

static void *pb_reader(void *arg)
{
    int chn = (int)(intptr_t)arg;

    pthread_mutex_lock(&g.lock);
    struct rsdk_group *grp = g.group;
    uint32_t start        = g.slot[chn].pb_start;
    uint32_t pb_end       = g.slot[chn].pb_end;    /* 事件回放止点(0=时间轴回放不限) */
    int      pbstream     = g.slot[chn].pb_stream;
    pthread_mutex_unlock(&g.lock);

    if (!grp) {
        NVR_LOGW("rtsp", "pb ch%d: 无盘组,退出", chn);
        return NULL;
    }

    rsdk_index_slot_t *segs = (rsdk_index_slot_t *)malloc(sizeof(*segs) * PB_MAX_SEGS);
    if (!segs) return NULL;

    /* 段起点即 IDR(含参数集),从首帧直播;SDP sprop 由服务端从关键帧生成。 */
    rsdk_group_player_t *pl = pb_open_at(grp, chn, pbstream, start, segs);

    uint64_t prev_pts = 0;
    int      have_prev = 0;
    int      inited    = 0;
    /* RTP 时戳累加器(90kHz):基点=当前定位点 UTC×90k(绝对,便于 APP 映射真实录像时间),
     * 每帧按夹钳后的真实帧间隔累加 → 单调、1×、跨段/回绕不跳 → 修"时间轴不按录像顺序叠加"。 */
    uint32_t pb_rtp = (uint32_t)((uint64_t)start * 90000u);

    while (g.slot[chn].pb_run) {
        /* ── seek:APP 拖时间轴 → SET_PARAMETER(playback_ctrl:seek) → 重定位到新 UTC。 ── */
        if (g.slot[chn].pb_seek_req) {
            uint32_t sk = g.slot[chn].pb_seek_utc;
            g.slot[chn].pb_seek_req = 0;
            if (pl) { rsdk_group_play_close(pl); pl = NULL; }
            pl = pb_open_at(grp, chn, pbstream, sk, segs);
            prev_pts = 0; have_prev = 0;
            pb_rtp = (uint32_t)((uint64_t)sk * 90000u);   /* 时间轴基点跳到 seek 点 */
            NVR_LOGD("playback", "pb ch%d seek → %u (%s)", chn, sk, pl ? "有段" : "无段hold");
        }

        /* seek 后暂停:等 APP 发 play(playback_ctrl:play 或 PLAY)再推。 */
        if (g.slot[chn].pb_paused) { usleep(50 * 1000); continue; }

        /* 无录像位置:hold(空白帧待实现),保持线程存活以响应后续 seek。 */
        if (!pl) { usleep(100 * 1000); continue; }

        rsdk_frame_hdr_t h;
        const uint8_t   *data = NULL;
        uint32_t         len  = 0;
        int              disk = 0;
        rsdk_err_t rc = rsdk_group_play_next(pl, &h, &data, &len, &disk);
        if (rc == RSDK_E_NOTFOUND) {                       /* 录像放完:hold,等 seek/停止 */
            rsdk_group_play_close(pl); pl = NULL;
            continue;
        }
        if (rc != RSDK_OK || !data || len == 0) { rsdk_group_play_close(pl); pl = NULL; continue; }
        if (h.rec_kind != RSDK_RK_FRAME) continue;        /* 跳过段起/止/事件等标记记录 */
        if ((int)h.stream != pbstream) continue;          /* 只出视频(音频 stream==2 忽略,见报告) */

        /* 事件回放:播到事件止点(end_time)即暂停——停在最后一帧,读线程存活以响应 seek/重播。
         * wall_time=0(未写)时条件不成立,不会误停。 */
        if (pb_end && (uint32_t)h.wall_time >= pb_end) {
            rsdk_group_play_close(pl); pl = NULL;
            g.slot[chn].pb_paused = 1;
            NVR_LOGD("playback", "pb ch%d 事件录像播完 @%u(止%u)→ 暂停", chn,
                     (uint32_t)h.wall_time, pb_end);
            continue;
        }

        int vcodec = (h.codec == RSDK_CODEC_H265) ? 1 : 0;

        if (!inited) {
            pthread_mutex_lock(&g.lock);
            g.chan_codec[chn] = vcodec;
            pthread_mutex_unlock(&g.lock);
            /* 音频 v1 暂不出(段按 stream 分,单一 player 无法可靠交织音频轨)→ want_audio=0。 */
            int arc = nvr_srv_add_stream(chn, vcodec, 0, 0, 25.0, 0,
                                         0, NVR_SRV_ACODEC_AAC, 16000, 1);
            if (arc != 0) {
                NVR_LOGW("rtsp", "pb ch%d addStream %s 失败", chn, vcodec ? "H265" : "H264");
                break;
            }
            pthread_mutex_lock(&g.lock);
            g.slot[chn].inited = 1;
            pthread_mutex_unlock(&g.lock);
            inited = 1;
            NVR_LOGD("playback", "pb addStream slot%d ch%d st%d %s", chn, chn, pbstream,
                     vcodec ? "H265" : "H264");
        }

        /* 帧间隔(夹钳):既用于实时节拍,也用于累加 RTP 时戳,两者同源→时间轴与播放同步。 */
        long ms = 0;
        if (have_prev && h.pts >= prev_pts) {
            ms = (long)((h.pts - prev_pts) / 90u);
            if (ms < 0)   ms = 0;
            if (ms > 500) ms = 500;                        /* 回绕/跨段跳变不冻结、不快进 */
        }
        prev_pts  = h.pts;
        have_prev = 1;
        pb_rtp   += (uint32_t)(ms * 90);                   /* 90kHz 累加(首帧 ms=0,停在基点) */

        if (ms > 0) usleep((useconds_t)ms * 1000);         /* 实时节拍 */

        /* 睡醒后重新判定停止/暂停/seek:避免暂停期仍推一帧。 */
        if (!g.slot[chn].pb_run || g.slot[chn].pb_paused || g.slot[chn].pb_seek_req) continue;
        nvr_srv_push_video_ts(chn, data, len, pb_rtp);     /* 带真实媒体时戳出流 */
    }

    if (pl) rsdk_group_play_close(pl);
    free(segs);
    NVR_LOGD("playback", "pb reader ch%d 退出", chn);
    return NULL;
}

/* 启动某通道回放(调用方不得持 g.lock;内部自锁)。先夺回该 slot(释放任何直播占用),
 * 置 pb_mode 并起读线程。codec 首帧才知 → 不在此 addStream。 */
static int pb_start(int chn, uint32_t start, uint32_t end, int stream, int want_audio, int want_video)
{
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    int st = stream ? 1 : 0;

    pthread_mutex_lock(&g.lock);
    /* 夺回该通道 slot:若被直播占用先释放其服务端流(此时该 slot 必非 pb_mode——
     * 上层已 pb_stop_one 停掉旧回放)。 */
    if (g.slot[chn].used && !g.slot[chn].pb_mode)
        free_slot_locked(chn);
    memset(&g.slot[chn], 0, sizeof(g.slot[chn]));
    g.slot[chn].used         = 1;
    g.slot[chn].chn          = chn;
    g.slot[chn].stream       = st;
    g.slot[chn].pb_mode      = 1;
    g.slot[chn].pb_run       = 1;
    g.slot[chn].pb_start     = start;
    g.slot[chn].pb_end       = end;
    g.slot[chn].pb_stream    = st;
    g.slot[chn].pb_want_audio = want_audio ? 1 : 0;
    g.slot[chn].pb_want_video = want_video ? 1 : 0;
    int rc = pthread_create(&g.slot[chn].pb_tid, NULL, pb_reader, (void *)(intptr_t)chn);
    if (rc != 0) {
        memset(&g.slot[chn], 0, sizeof(g.slot[chn]));
        pthread_mutex_unlock(&g.lock);
        NVR_LOGE("rtsp", "pb ch%d 读线程创建失败(%d)", chn, rc);
        return -1;
    }
    pthread_mutex_unlock(&g.lock);
    NVR_LOGD("playback", "pb start ch%d st%d @%u", chn, st, start);
    return 0;
}

/* 停止单通道回放:置停止标志→join→释放 slot(含服务端流)。不持锁 join(避免死锁)。
 * 若该通道无回放,安全 no-op。 */
static void pb_stop_one(int chn)
{
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return;
    pthread_t tid;
    int have = 0;
    pthread_mutex_lock(&g.lock);
    if (g.slot[chn].pb_mode) {
        g.slot[chn].pb_run = 0;
        tid  = g.slot[chn].pb_tid;
        have = 1;
    }
    pthread_mutex_unlock(&g.lock);
    if (have) pthread_join(tid, NULL);
    pthread_mutex_lock(&g.lock);
    if (g.slot[chn].pb_mode)
        free_slot_locked(chn);            /* 释放服务端流并清 slot(含 pb_mode) */
    pthread_mutex_unlock(&g.lock);
}

/* 停止所有通道回放:批量置停止标志、快照句柄,释放锁后统一 join,再统一释放 slot。 */
static void pb_stop_all(void)
{
    pthread_t tids[NVR_SRV_MAX_SLOTS];
    int chns[NVR_SRV_MAX_SLOTS];
    int n = 0;
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < NVR_SRV_MAX_SLOTS; i++) {
        if (g.slot[i].pb_mode) {
            g.slot[i].pb_run = 0;
            tids[n] = g.slot[i].pb_tid;
            chns[n] = i;
            n++;
        }
    }
    pthread_mutex_unlock(&g.lock);
    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < n; i++)
        if (g.slot[chns[i]].pb_mode)
            free_slot_locked(chns[i]);
    pthread_mutex_unlock(&g.lock);
}

/* ---------------- 公开 API ---------------- */

void nvr_rtsp_live_select(int chn)
{
    if (chn < 0) {
        /* 取消:释放所有 live slot(回放独占的 slot 不动,由 stopPlayback 收) */
        pthread_mutex_lock(&g.lock);
        for (int i = 0; i < NVR_SRV_MAX_SLOTS; i++)
            if (!g.slot[i].pb_mode)
                free_slot_locked(i);
        pthread_mutex_unlock(&g.lock);
        return;
    }
    nvr_rtsp_live_select_media(chn, 0, 1, 1);
}

void nvr_rtsp_live_select_ex(int chn, int stream)
{
    nvr_rtsp_live_select_media(chn, stream, 1, 0);
}

void nvr_rtsp_live_select_media(int chn, int stream, int want_video, int want_audio)
{
    if (!g.run || chn < 0) return;
    int st = stream ? 1 : 0;
    pthread_mutex_lock(&g.lock);
    int s = alloc_slot_locked(chn, st, want_video, want_audio);
    pthread_mutex_unlock(&g.lock);
    if (s < 0)
        NVR_LOGW("rtsp", "select_media ch%d st%d: slot 池已满(%d)", chn, st, NVR_SRV_MAX_SLOTS);
}

int nvr_rtsp_live_port(void)
{
    return g.run ? g.port : 0;
}

/* 返回 (chn,stream) 当前占用的 slot 索引(== chn);未分配返回 -1。 */
int nvr_rtsp_live_slot_of(int chn, int stream)
{
    if (!g.run || chn < 0) return -1;
    int st = stream ? 1 : 0;
    pthread_mutex_lock(&g.lock);
    int s = slot_of_locked(chn, st);
    pthread_mutex_unlock(&g.lock);
    return s;
}

/* 返回该通道最近观测到的视频 codec(0=H264 1=H265,-1=未知)。startLiveStream
 * 据此拼 ch<N>_<S>.264 / .265("按视频类型")。未拉过流时返回 -1(调用方默认 264)。 */
int nvr_rtsp_live_codec_of(int chn)
{
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    pthread_mutex_lock(&g.lock);
    int c = g.chan_codec[chn];
    pthread_mutex_unlock(&g.lock);
    return c;
}

/* 释放某通道两条码流(主/子)占用的 live slot;stopLiveStream 带 channel 时用,
 * 避免 select(-1) 误伤其它并发观看的通道。 */
void nvr_rtsp_live_release(int chn)
{
    if (chn < 0) return;
    pthread_mutex_lock(&g.lock);
    /* 回放独占该通道时,stopLiveStream 不应误停回放。 */
    if (!g.slot[chn].pb_mode) {
        for (int st = 0; st < 2; st++) {
            int s = slot_of_locked(chn, st);
            if (s >= 0) free_slot_locked(s);
        }
    }
    pthread_mutex_unlock(&g.lock);
}

void nvr_rtsp_live_set_group(struct rsdk_group *group)
{
    if (!g.run) {
        g.group = group;
        return;
    }
    pthread_mutex_lock(&g.lock);
    g.group = group;
    pthread_mutex_unlock(&g.lock);
}

int nvr_rtsp_pb_prepare(int chn, uint32_t start_utc, int stream, int want_audio, int want_video,
                        int by_event, uint32_t *duration_out)
{
    pthread_mutex_lock(&g.lock);
    int i = g.pb_bind_i % 8;
    g.pb_bind[i].start = start_utc;
    g.pb_bind[i].chn = chn;
    g.pb_bind[i].stream = (stream ? 1 : 0);
    g.pb_bind[i].want_audio = want_audio ? 1 : 0;
    g.pb_bind[i].want_video = want_video ? 1 : 0;
    g.pb_bind[i].used = 1;
    g.pb_bind_i = i + 1;
    struct rsdk_group *grp = g.group;
    pthread_mutex_unlock(&g.lock);

    /* 事件回放(带 fileName):duration=事件时长,止点=事件 end;找不到事件则回落连续段(不设止点)。
     * 时间轴回放:duration=连续段剩余秒,不设止点。 */
    uint32_t end_utc = 0;
    uint32_t d = 0;
    if (by_event) d = pb_event_window_of(grp, chn, start_utc, &end_utc);
    if (d == 0) { d = pb_duration_of(grp, chn, start_utc); end_utc = 0; }
    if (duration_out) *duration_out = d;

    /* 真正出流:先停掉本通道任何在跑的回放(seek = 用新起点重发 startPlayback → 重启读线程),
     * 再按新起点起读线程。URL 在返回后即可拉(库侧 ch<N>_<S> 路由到 slot=chn;首帧到达时
     * 读线程 addStream 写入真实 codec,SDP 携带真值)。 */
    if (!g.run) return 0;
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    pb_stop_one(chn);
    return pb_start(chn, start_utc, end_utc, stream, want_audio, want_video);
}

void nvr_rtsp_pb_stop(void)
{
    /* 停止所有回放读线程并释放其 slot(无回放时安全 no-op)。pb_bind 登记不清(旧语义)。 */
    pb_stop_all();
}

/* 回放 URL 解析器(库侧在处理 DESCRIBE rtsp://.../playback/<startTime> 时回调本函数)。
 * 把 startTime 反查回通道号(== slot):优先精确匹配登记表 pb_bind.start,否则取最近一次
 * startPlayback 的通道。返回 slot 前需等读盘线程 addStream 完成(inited)——否则库侧
 * isInited() 失败会 404;pb_prepare 已同步起线程,这里最多等 ~2s(读首个关键帧的时间)。
 * 不在 DESCRIBE 线程里持锁睡眠。返回 slot(==chn)或 <0(未知/未就绪)。 */
static int nvr_pb_resolve_slot(unsigned int start_ts)
{
    int chn = -1;
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < 8; i++) {
        if (g.pb_bind[i].used && g.pb_bind[i].start == start_ts) { chn = g.pb_bind[i].chn; break; }
    }
    if (chn < 0) {                                  /* 回退:最近一次 startPlayback */
        int last = (g.pb_bind_i + 7) % 8;
        if (g.pb_bind[last].used) chn = g.pb_bind[last].chn;
    }
    pthread_mutex_unlock(&g.lock);
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;

    for (int tries = 0; tries < 40; tries++) {      /* ~2s:40 × 50ms */
        int ready = 0, alive = 0;
        pthread_mutex_lock(&g.lock);
        alive = (g.slot[chn].pb_mode && g.slot[chn].used);
        ready = (alive && g.slot[chn].inited);
        pthread_mutex_unlock(&g.lock);
        if (ready) return chn;
        if (!alive) return -1;                      /* 读线程已退出(无录像等)→ 未就绪 */
        usleep(50 * 1000);
    }
    return -1;                                      /* 超时未 addStream(该时间点可能无录像) */
}

/* 回放控制(库侧 SET_PARAMETER 处理时回调):op 0=seek(到 utc)、1=play(恢复推流)。
 * chn = 会话的 v_index(==slot)。非回放 slot 返回 -1(库侧据此对直播 slot 不做处理)。
 * seek:置目标 + 请求重定位 + 暂停(读线程重开 player 并 hold,等 play);play:清暂停。 */
static int nvr_pb_ctrl(int chn, int op, unsigned int utc)
{
    if (chn < 0 || chn >= NVR_SRV_MAX_SLOTS) return -1;
    int rc = -1;
    pthread_mutex_lock(&g.lock);
    if (g.slot[chn].used && g.slot[chn].pb_mode) {
        if (op == 0) {                     /* seek */
            g.slot[chn].pb_seek_utc = utc;
            g.slot[chn].pb_seek_req = 1;
            g.slot[chn].pb_paused   = 1;
            rc = 0;
        } else if (op == 1) {              /* play / resume */
            g.slot[chn].pb_paused   = 0;
            rc = 0;
        }
    }
    pthread_mutex_unlock(&g.lock);
    if (rc == 0)
        NVR_LOGD("playback", "pb ctrl ch%d op=%s utc=%u", chn, op == 0 ? "seek" : "play", utc);
    return rc;
}

/* SET_PARAMETER body 诊断 → 分类 DEBUG("rtsp");默认静默,NVR_LOG_CATS=rtsp 时可见。 */
static void nvr_pb_diag(int is_live, int ctx_len, const char *buf)
{
    const char *b = buf ? buf : "";
    int has = (strstr(b, "playback_ctrl: seek") || strstr(b, "playback_ctrl:seek")) ? 1 : 0;
    NVR_LOGD("rtsp", "SET_PARAMETER is_live=%d ctx_len=%d has_seek=%d body=<%.80s>",
             is_live, ctx_len, has, b);
}

int nvr_rtsp_live_start(int port)
{
    if (g.run) return 0;
    if (port <= 0) port = 8554;

    struct rsdk_group *prev_group = g.group;   /* 保留 start 前已绑定的盘组 */
    memset(&g, 0, sizeof(g));
    g.group = prev_group;
    g.port = port;
    for (int i = 0; i < NVR_SRV_MAX_SLOTS; i++) g.chan_codec[i] = -1;  /* 未知 */
    pthread_mutex_init(&g.lock, NULL);

    int actual = nvr_srv_start(port);
    if (actual <= 0) {
        pthread_mutex_destroy(&g.lock);
        NVR_LOGE("rtsp", "NopRtspServer start :%d 失败", port);
        return -1;
    }
    g.port = actual;
    g.run = 1;
    rtsp_set_pb_slot_resolver(nvr_pb_resolve_slot);   /* playback/<ts> → slot 反查 */
    rtsp_set_pb_ctrl_hook(nvr_pb_ctrl);               /* SET_PARAMETER seek/play → 读线程 */
    rtsp_set_pb_diag_hook(nvr_pb_diag);               /* SET_PARAMETER body → 分类 DEBUG(rtsp) */
    NVR_LOGI("rtsp", "NopRtspServer listen :%d (live + playback + seek)", actual);
    return 0;
}

void nvr_rtsp_live_stop(void)
{
    if (!g.run) return;
    g.run = 0;
    pb_stop_all();                     /* 先停回放读线程并 join(不持锁,避免死锁) */
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < NVR_SRV_MAX_SLOTS; i++)
        free_slot_locked(i);
    pthread_mutex_unlock(&g.lock);
    nvr_srv_stop();
    pthread_mutex_destroy(&g.lock);
}

void nvr_rtsp_live_feed(int chn, int stream, const uint8_t *data, int len,
                        int codec, int is_key, uint32_t ts_ms)
{
    (void)ts_ms;   /* 服务端用挂钟打时戳并节拍;相机侧 RTP ts 不再透传 */
    if (!g.run || !data || len <= 0) return;
    int st = stream ? 1 : 0;

    pthread_mutex_lock(&g.lock);
    /* 记录该通道 codec(录像喂流常开→所有在拉通道都会被记录),供 startLiveStream 选扩展名。
     * 只记主流(st==0)的 codec 作为通道代表:主 H265+子 H264 很常见,若不限定会被子流
     * (通常 H264)覆盖成 264,导致 startLiveStream 对 H265 通道回错扩展名。 */
    if (st == 0 && chn >= 0 && chn < NVR_SRV_MAX_SLOTS)
        g.chan_codec[chn] = (codec == 1) ? 1 : 0;
    int s = slot_of_locked(chn, st);
    if (s < 0) { pthread_mutex_unlock(&g.lock); return; }
    /* 该 slot 被回放独占 → 直播帧不得推入(回放读线程独占推流)。 */
    if (g.slot[s].pb_mode) { pthread_mutex_unlock(&g.lock); return; }
    if (!g.slot[s].inited) {
        if (!is_key) { pthread_mutex_unlock(&g.lock); return; }  /* 等关键帧再注册 */
        /* w/h 交由 SDK param_check 默认(挂钟计时,维度非节拍关键;本文件无 SPS 几何解析
         * 器,按 brief 传 0,0)。fps 仅为 SDP 提示,传 25。 */
        int rc = nvr_srv_add_stream(s, codec, 0, 0, 25.0, 0,
                                    g.slot[s].has_audio, NVR_SRV_ACODEC_AAC, 16000, 1);
        if (rc != 0) {
            pthread_mutex_unlock(&g.lock);
            NVR_LOGW("rtsp", "addStream slot%d ch%d st%d %s 失败", s, chn, st,
                     codec == 1 ? "H265" : "H264");
            return;
        }
        g.slot[s].inited = 1;
        NVR_LOGI("rtsp", "addStream slot%d ch%d st%d %s audio=%d → live%d",
                 s, chn, st, codec == 1 ? "H265" : "H264", g.slot[s].has_audio, s + 1);
    }
    pthread_mutex_unlock(&g.lock);

    /* Annex-B 原样推入,服务端负责 NAL 拆分/打包/打时戳。key 与非 key 都推。 */
    nvr_srv_push_video(s, data, len);
}

void nvr_rtsp_live_feed_audio(int chn, const uint8_t *data, int len, uint32_t ts_ms)
{
    (void)ts_ms;
    if (!g.run || !data || len <= 0) return;
    pthread_mutex_lock(&g.lock);
    int s = slot_of_locked(chn, 0 /* 音频挂主流 */);
    int ok = (s >= 0 && g.slot[s].used && !g.slot[s].pb_mode &&
              g.slot[s].has_audio && g.slot[s].inited);
    pthread_mutex_unlock(&g.lock);
    if (ok)
        nvr_srv_push_audio(s, data, len, NVR_AAC_NBSAMPLES);
}
