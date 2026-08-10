/***************************************************************************************
 *  stream_mgr.cpp — 32 路拉流生命周期管理（实现 nvr_streaming.h 的 C API）
 *
 *  双流:每通道主+子两路 puller。channel 层解析出主/子 URL 后经 nvr_stream_set_url 分别提供,
 *  streaming 各自起 puller。显示切换 nvr_stream_set_decode_stream:改喂解码器的码流(两路都在拉,
 *  瞬时,不重连)。set_display 门控解码(可见才解)。
 ***************************************************************************************/
#include "stream_internal.h"
#include "mhal_vout.h"
#include "nvr_log.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>   /* usleep:格式化暂停写盘的 drain */

extern "C" int  stream_pull_start(stream_pull_t *p, int conn_to, int rx_to);
extern "C" void stream_pull_stop (stream_pull_t *p);
extern "C" int  puller_start(stream_chan_t *c, int conn_to, int rx_to, rsdk_group_t *grp);
extern "C" void puller_stop (stream_chan_t *c);
extern "C" void stream_close_writer(stream_chan_t *c);   /* stream_router.c:安全关闭 writer */

struct nvr_stream_mgr {
    nvr_stream_mgr_cfg_t cfg;
    stream_chan_t        ch[NVR_MAX_CH];
    int                  used[NVR_MAX_CH];
    int                  active[NVR_MAX_CH];   /* 1=已 start(允许起 puller) */
};

static stream_chan_t *slot(nvr_stream_mgr_t *m, int chn)
{
    if (!m || chn < 0 || chn >= NVR_MAX_CH || !m->used[chn]) return NULL;
    return &m->ch[chn];
}
static stream_pull_t *pull_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
}

extern "C" rsdk_err_t nvr_stream_mgr_init(const nvr_stream_mgr_cfg_t *cfg, nvr_stream_mgr_t **out)
{
    if (!cfg || !out) return RSDK_E_PARAM;
    nvr_stream_mgr_t *m = (nvr_stream_mgr_t *)calloc(1, sizeof(*m));
    if (!m) return RSDK_E_NOSPACE;
    m->cfg = *cfg;
    if (m->cfg.conn_timeout <= 0) m->cfg.conn_timeout = 5;
    if (m->cfg.rx_timeout   <= 0) m->cfg.rx_timeout   = 10;
    *out = m;
    return RSDK_OK;
}

extern "C" void nvr_stream_mgr_deinit(nvr_stream_mgr_t *m)
{
    if (!m) return;
    nvr_stream_stop_all(m);
    free(m);
}

extern "C" rsdk_err_t nvr_stream_add_channel(nvr_stream_mgr_t *m, const nvr_stream_chan_cfg_t *c)
{
    if (!m || !c || c->chn < 0 || c->chn >= NVR_MAX_CH) return RSDK_E_PARAM;
    stream_chan_t *s = &m->ch[c->chn];
    memset(s, 0, sizeof(*s));
    s->cfg   = *c;
    s->state = NVR_CH_IDLE;
    s->mgr   = m;
    s->show_win = -1;                 /* 默认隐藏,由 preview 点亮 */
    s->decode_stream = NVR_STREAM_SUB;/* 默认多宫格=子码流;单宫格时 preview 切主 */
    s->pmain.owner = s; s->pmain.stream = NVR_STREAM_MAIN; s->pmain.codec = -1;
    s->psub.owner  = s; s->psub.stream  = NVR_STREAM_SUB;  s->psub.codec  = -1;
    /* cfg.url(若给)按 cfg.stream 落到对应路;通常由 channel 层随后 set_url 提供主/子两条。 */
    if (c->url[0]) snprintf(pull_of(s, c->stream)->url, sizeof(s->pmain.url), "%s", c->url);
    m->used[c->chn] = 1;
    return RSDK_OK;
}

/* channel 层解析出某码流 URL → 提供给 streaming;通道已 active 则立即起该路 puller。 */
extern "C" rsdk_err_t nvr_stream_set_url(nvr_stream_mgr_t *m, int chn, int stream, const char *url)
{
    stream_chan_t *c = slot(m, chn);
    if (!c || !url || !url[0]) return RSDK_E_PARAM;
    stream_pull_t *p = pull_of(c, stream);
    if (strcmp(p->url, url) == 0 && p->puller) return RSDK_OK;   /* 同 URL 且在跑 */
    if (p->puller) stream_pull_stop(p);
    snprintf(p->url, sizeof(p->url), "%s", url);
    if (m->active[chn]) stream_pull_start(p, m->cfg.conn_timeout, m->cfg.rx_timeout);
    return RSDK_OK;
}

/* 显示门控(preview 驱动):设当前显示格。win>=0 开解码(用 decode_stream 那路),win<0 关解码。 */
extern "C" rsdk_err_t nvr_stream_set_display(nvr_stream_mgr_t *m, int chn, int win)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    c->show_win = win;
    if (!c->router_open) return RSDK_OK;      /* 未连上:连上后由 pull_on_connected 兑现 */
    if (win >= 0) {
        if (!c->vdec) stream_decode_open(c, win);
        else          mhal_vout_bind(win, chn);
    } else {
        stream_decode_close(c);
    }
    return RSDK_OK;
}

/* ★ 切换喂解码器的码流(单宫格=主/多宫格=子)。两路都在拉 → 瞬时,不重连。
 * 若正在解码,关旧解码器再用新码流开(分辨率不同需重开;流已在,立即出图)。 */
extern "C" rsdk_err_t nvr_stream_set_decode_stream(nvr_stream_mgr_t *m, int chn, int stream)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    if (c->decode_stream == stream) return RSDK_OK;
    c->decode_stream = stream;
    if (c->vdec && c->show_win >= 0) {        /* 正在显示 → 换解码源重开(用新码流 codec/分辨率) */
        int win = c->show_win;
        stream_decode_close(c);
        stream_decode_open(c, win);
    }
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_start(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    m->active[chn] = 1;
    c->grp = m->cfg.group;
    return puller_start(c, m->cfg.conn_timeout, m->cfg.rx_timeout, m->cfg.group) == 0 ? RSDK_OK : RSDK_E_IO;
}

extern "C" rsdk_err_t nvr_stream_stop(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    m->active[chn] = 0;
    puller_stop(c);
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_start_all(nvr_stream_mgr_t *m)
{
    if (!m) return RSDK_E_PARAM;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->used[i]) nvr_stream_start(m, i);
    return RSDK_OK;
}

extern "C" void nvr_stream_stop_all(nvr_stream_mgr_t *m)
{
    if (!m) return;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->used[i]) { m->active[i] = 0; puller_stop(&m->ch[i]); }
}

/* 兼容旧 API:直接给某码流 URL(=set_url + 记 decode_stream)。 */
extern "C" rsdk_err_t nvr_stream_switch_stream(nvr_stream_mgr_t *m, int chn, int stream, const char *url)
{
    if (url && url[0]) nvr_stream_set_url(m, chn, stream, url);
    return nvr_stream_set_decode_stream(m, chn, stream);
}

/* 事件标记(命令/事件线程调用,只置 pend_*;puller 线程 owns writer 时应用写盘,避免并发):
 * event_id=0 清除标签;否则打标 + 写内联 EVENT 记录(供 queryEventList/scan)。end 为事件窗口末(自动清)。 */
extern "C" rsdk_err_t nvr_stream_set_event(nvr_stream_mgr_t *m, int chn, uint64_t event_id,
                                           int rectype, uint32_t start, uint32_t end)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    c->pend_event_rectype = (uint8_t)rectype;
    c->pend_event_start   = start;
    c->pend_event_end     = end;
    c->pend_event_id      = event_id;   /* 最后置 id(puller 见 pend!=applied 触发应用) */
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_set_record(nvr_stream_mgr_t *m, int chn, int on)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    /* ★ 线程安全:writer 由 puller 线程独占写。命令线程**只置标志**,绝不在此 close(否则 puller
     * 正在 rsdk_rec_write_frame 时被 free → use-after-free 崩溃/SIGSEGV)。
     *   开:置 record=1 + 若无 writer 则补开(仅指针赋值,puller 读到 NULL/新值都安全,不是 free);
     *       并复位关键帧门控,让录像从下一个 IDR 干净起。
     *   关:仅置 record=0 → stream_route_video 见 !record 跳过写入(不 close);writer 保留,
     *       通道拆除(puller 已停)时才由 stream_router_close 关,无竞态。 */
    c->cfg.record = on;
    if (on) {
        if (!c->writer) stream_open_writer(c, m->cfg.group);
        c->rec_gated_main = 0; c->rec_gated_sub = 0;
    }
    return RSDK_OK;
}

/* 录像中通道位图:bit chn = 该通道 writer 已开(正在写盘)。供 GUI_longPolling 的 RecordStatus
 * (协议:LSB0=通道1... bit chn0=通道 chn0+1)→ GUI 显示录像图标。 */
extern "C" uint32_t nvr_stream_recording_mask(nvr_stream_mgr_t *m)
{
    uint32_t mask = 0;
    if (!m) return 0;
    for (int i = 0; i < NVR_MAX_CH && i < 32; i++)
        if (m->used[i] && m->ch[i].writer && m->ch[i].cfg.record) mask |= (1u << i);  /* 开关关=不算录像中 */
    return mask;
}

/* 运行时更新录像盘组(格式化后重组装用):设新 group + 对所有录像通道补开 writer(免重启即录)。
 * 开机盘未格式化时 group=NULL、各通道 writer 没开;格式化+assemble 后调此,ch 立即开始写盘。 */
extern "C" void stream_open_writer(stream_chan_t *c, rsdk_group_t *grp);   /* stream_router.c */
extern "C" rsdk_err_t nvr_stream_mgr_set_group(nvr_stream_mgr_t *m, rsdk_group_t *group)
{
    if (!m) return RSDK_E_PARAM;
    m->cfg.group = group;
    int opened = 0;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        stream_chan_t *c = &m->ch[i];
        c->grp = group;
        if (group && c->cfg.record && !c->writer) { stream_open_writer(c, group); opened++; }
    }
    NVR_LOGI("stream", "set_group: 更新录像盘组, 补开 %d 路 writer", opened);
    return RSDK_OK;
}

/* 格式化前:暂停所有写盘并关闭 writer(命令线程调用)。
 * 机制沿用既有无锁契约:置 cfg.record=0 → puller(stream_route_video)见 !record 即跳过写盘、
 * 不再触碰 writer;短暂 drain 让已越过 record 检查的在途 write_frame 落地;此后 puller 不碰 writer,
 * 命令线程关闭全部 writer 安全(无 use-after-free)。返回暂停前处于录像的通道位图,供 resume 恢复。 */
extern "C" uint32_t nvr_stream_mgr_pause_recording(nvr_stream_mgr_t *m)
{
    if (!m) return 0;
    uint32_t was = 0;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        stream_chan_t *c = &m->ch[i];
        if (c->cfg.record) was |= (1u << i);
        c->cfg.record = 0;            /* puller 下一帧起跳过写盘 */
    }
    usleep(150 * 1000);               /* drain:等已越过 record 检查的在途 write_frame 完成 */
    int closed = 0;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        if (m->ch[i].writer) { stream_close_writer(&m->ch[i]); closed++; }
    }
    NVR_LOGW("stream", "格式化暂停写盘:置 record=0 + 关闭 %d 路 writer(盘静默)", closed);
    return was;
}

/* 格式化后:恢复原录像通道 + 换新盘组补开 writer(免重启即录)。 */
extern "C" void nvr_stream_mgr_resume_recording(nvr_stream_mgr_t *m, rsdk_group_t *group, uint32_t was)
{
    if (!m) return;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        if (was & (1u << i)) m->ch[i].cfg.record = 1;   /* 恢复录像开关 */
    }
    nvr_stream_mgr_set_group(m, group);                 /* 对 record 通道在新组补开 writer */
    NVR_LOGW("stream", "格式化恢复写盘:恢复录像通道位图 0x%x", was);
}

/* 回放引擎:取某通道某码流的解码尺寸(内部封装 resolve_dim)。 */
extern "C" void stream_chan_get_dim(stream_chan_t *c, int stream, int *w, int *h, int *fps);
extern "C" int nvr_stream_dim(nvr_stream_mgr_t *m, int chn, int stream, int *w, int *h, int *fps)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) { *w = 1920; *h = 1080; *fps = 25; return -1; }
    stream_chan_get_dim(c, stream, w, h, fps);
    return 0;
}

extern "C" nvr_ch_state_t nvr_stream_state(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    return c ? c->state : NVR_CH_IDLE;
}

extern "C" int nvr_stream_decode_denied(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    return c ? c->decode_denied : 0;
}

/* 该通道当前是否"已出图"(解码器已开且喂到帧)。供切宫格接口阻塞回复:等到有格出图再回。 */
extern "C" int nvr_stream_display_ready(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    return (c && c->vdec && c->fed_since_open > 0) ? 1 : 0;
}

/* 给该通道喂缓存关键帧(批量提交 defer_end、解码器已 start 之后调)→ 秒出图。 */
extern "C" rsdk_err_t nvr_stream_feed_keyframe(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    stream_feed_keyframe(c);
    return RSDK_OK;
}
