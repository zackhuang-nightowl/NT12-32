/***************************************************************************************
 *  mhal_internal.h — media_hal 内部共享状态（不对外）
 *
 *  管线（参照 hdal 样例 playback_1div_to_4div.c）：
 *     videodec ──bind──► videoproc(VPE) ──bind──► videoout(HDMI/CVBS)
 *  每通道一条 dec→proc→vout 路径；vout 设备与分屏布局全局管理。
 ***************************************************************************************/
#ifndef MHAL_INTERNAL_H
#define MHAL_INTERNAL_H

#include "hdal.h"                 /* hd_* 全量 API（BSP: code/hdal/include） */
#include "mhal_vdec.h"
#include "mhal_vout.h"
#include <pthread.h>
#include <stdint.h>

#define MHAL_MAX_CH 32                   /* 32 路（分屏预览最多 32 解码器）*/
#define MHAL_COMMIT_DEBOUNCE_MS 150      /* 防抖窗:变更静默此时长后才重建一次(合并突发开/关) */
#define MHAL_COMMIT_MAX_MS      600      /* 最大合并窗:首次请求起最迟此时限必重建一次(防持续上线饿死) */

/* ★ 预建全窗(prebuild)架构:开机一次性把 N 路 dec/proc/vout 全部 open+bind+start 进**同一张**合成图
 * (single VPD large graph),之后运行期加/删/切设备**只逐窗 IN_WIN_ATTR(visible/rect)+ proc OUT(rect)**,
 * 永不再 stop_list/start_list 整图重建 → "一路问题只影响一路"(见官方样例 playback_1div_to_4div.c)。
 * 解码器按**预留档 768×576(64 对齐,覆盖 640x360/704x480/704x576/720x480 主力子流)**分配 max_mem;
 * 实际流 ≤ 预留 → 直接喂零重配;> 预留(1280x720/1080x1080 等少数)→ 只重配那一路(stop dec→cfg→start dec,
 * 不碰 proc/vout、不碰别路)。DEC_OUT 32×768x576 ≈ 64MB < 池;不扩 DTB。 */
#define MHAL_DEC_RSV_W 768               /* 预留解码宽(align64(720)=768) */
#define MHAL_DEC_RSV_H 576               /* 预留解码高(覆盖 704x576) */

/* 单通道解码路径 */
struct mhal_vdec {
    int          chn;
    int          opened;
    HD_VIDEO_CODEC codec;                /* HD_CODEC_TYPE_H264 / H265 (枚举 typedef 名为 HD_VIDEO_CODEC) */
    int          vout_win;               /* 绑定的分屏窗口；-1=不上屏 */
    int          w, h, fps;              /* 实际分辨率/帧率（预算准入 + max_mem 动态分配）*/
    HD_PATH_ID   dec_path;               /* videodec */
    HD_PATH_ID   proc_path;              /* videoproc(VPE) */
    HD_PATH_ID   vout_path;              /* videoout 输入路径 */
    int          crop_on;                /* 数字变焦:IN_CROP 是否已从 OFF 切到 ON(切模式需 start，只切一次) */
    int          budget_released;        /* 解码预算是否已归还(关闭即还,拆解幂等,防 defer 期间占额→回放误判超预算) */
    int          cfg_w, cfg_h;           /* ★ prebuild:当前已分配 max_mem 的对齐尺寸(≥预留)。实际流 ≤ 此则零重配;超此才重配该路。 */
    int          active;                 /* ★ prebuild:1=已激活(可见/在喂),0=预建但隐藏空闲(解码器常驻、不占预算/不喂帧) */
};

/* 全局显示设备状态 */
typedef struct {
    int                 inited;
    mhal_out_t          out;             /* HDMI/CVBS/VGA */
    HD_PATH_ID          ctrl_path;       /* videoout 设备控制路径 */
    HD_VIDEOOUT_SYSCAPS syscaps;         /* 含 input_dim（显示分辨率） */
    HD_FB_FMT           fb_fmt;
    int                 disp_w, disp_h;
    mhal_layout_t       layout;          /* 当前分屏 */
    struct mhal_vdec   *ch[MHAL_MAX_CH]; /* 已开的解码通道（按 chn 索引） */
    int                 prebuilt;        /* ★ 1=已开机预建全窗(单图)。此后 open/close 只逐窗 visible/rect,不整图重建。 */

    /* 已 start_list 的显示集合快照（SDK 一次成图流程：stop_list 旧集合 → start_list 新集合，
     * stop 在 start 前 → 任一时刻只需 1 块 VPD_graph_info_large(reserve=1)，
     * 根治边跑边单启触发的 "Can't extend"。见 mhal_vout_commit / SDK stop_module+start_module）。 */
    HD_PATH_ID          started_dec[MHAL_MAX_CH];
    HD_PATH_ID          started_proc[MHAL_MAX_CH];
    HD_PATH_ID          started_vout[MHAL_MAX_CH];
    int                 started_n;

    /* ★ 批量提交:defer>0 时,vdec_open/close 不各自 commit(只改 g_disp.ch/挂 pending),
     * 由 mhal_vout_defer_end 一次 commit(停旧集合→释放 pending→起新集合)。一次切宫格只重成图 1 次,
     * 9 个格一起出、只闪一次(而非一个一个各闪)。 */
    int                 defer;
    struct mhal_vdec   *pending_free[MHAL_MAX_CH];  /* defer 期间关闭、待 commit 后统一释放的解码器 */
    int                 pending_n;

    /* ★ 清黑门控:整屏 clear_black 只在"可能出现空格"时做(布局/分辨率变化、窗口被移除)。
     * 纯增量加窗(新设备上线落格)不清 → 避免把已有窗一起刷黑造成"整屏黑闪"。
     * 由 set_layout/set_resolution/init 置 1;commit 用后清 0(或按集合缩小自判)。 */
    int                 need_clear;

    /* ★ 防抖单次重建(真机验证:运行中只能整体 stop_list→start_list 重建, 不能单路 start)。
     * 各 puller 线程异步开/关解码器时只 mhal_vout_request_commit()(打 dirty+时戳), 不各自 commit;
     * 单一显示线程 commit_th 在变更静默 MHAL_COMMIT_DEBOUNCE_MS 后**只重建一次** →
     * 消除"每路各自重建"的 Apply 洪水与整屏黑闪。对齐 SDK 样例"建好后一次 start_list"的精神。 */
    volatile int        commit_pending;
    uint64_t            commit_req_ms;    /* 最近一次请求时戳(静默判定) */
    uint64_t            commit_first_ms;  /* 本轮首次请求时戳(最大合并窗判定, 防饿死) */
    pthread_t           commit_th;
    volatile int        commit_run;
    pthread_mutex_t     commit_mtx;
    pthread_cond_t      commit_cv;
    /* ★ commit 代数:每次 commit(全停 started 集合→全起新集合)都会**重启所有可见解码器**,
     * 重启后的解码器需重新喂关键帧才出图。此计数每成功 start_list 一次自增,供上层(streaming)
     * 比对——发现代数变了即知本路解码器被 commit 重启过 → 重灌缓存 IDR 秒 repaint,不留黑。 */
    volatile unsigned   commit_gen;
} mhal_disp_t;

/* 请求一次(防抖合并的)显示重建: 打 dirty + 时戳并唤醒 commit 线程。异步开/关解码器/切布局都用它,
 * 不要直接调 mhal_vout_commit(那会每次都整屏重建 → 洪水/黑闪)。 */
void mhal_vout_request_commit(void);

/* 重建显示合成图：把当前所有在显通道 dec/proc/vout 路径一次 stop_list→start_list。
 * 通道开/关（增删窗口路径）后调用；纯改窗口矩形/可见性不需要（走 IN_WIN_ATTR，不重启）。 */
int mhal_vout_commit(void);

/* 解码器刚 open、尚未 start_list 时把上次 ZoomPan 的 IN_CROP 写上路（调用方已持 mhal_lock）。 */
void mhal_crop_apply_pending(struct mhal_vdec *d);

/* g_disp 全局锁（递归）：多线程访问 —— 预览(8089路由线程) / 各通道 RTSP rx 线程(开关解码器)
 * / 主循环，都会碰 g_disp/videoout。公有入口加锁串行化，避免并发 commit 撕裂 started 集合。
 * 递归锁：open 内部会再调 bind/commit 等已加锁入口。init 时创建。 */
void mhal_lock(void);
void mhal_unlock(void);

extern mhal_disp_t g_disp;

/* 布局：算窗口 idx 在显示区内的矩形 */
void mhal_layout_rect(mhal_layout_t layout, int idx, int disp_w, int disp_h,
                      int *x, int *y, int *w, int *h);

#endif
