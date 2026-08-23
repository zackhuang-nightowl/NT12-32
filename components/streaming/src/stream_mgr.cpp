/***************************************************************************************
 *  stream_mgr.cpp — 32 路拉流生命周期管理（实现 nvr_streaming.h 的 C API）
 *
 *  双流:每通道主+子两路 puller。channel 层解析出主/子 URL 后经 nvr_stream_set_url 分别提供,
 *  streaming 各自起 puller。显示切换 nvr_stream_set_decode_stream:改喂解码器的码流(两路都在拉,
 *  瞬时,不重连)。set_display 门控解码(可见才解)。
 ***************************************************************************************/
#include "stream_internal.h"
#include "stream_hub.h"
#include "nvr_log.h"
#include <stdlib.h>
#include <cstring>
#include <cstdio>
#include <unistd.h>   /* usleep:格式化暂停写盘的 drain */

extern "C" {
#include "stream_record_worker.h"
}

extern "C" int  stream_pull_start(stream_pull_t *p, int conn_to, int rx_to);
extern "C" void stream_pull_stop (stream_pull_t *p);
extern "C" int  puller_start(stream_chan_t *c, int conn_to, int rx_to, rsdk_group_t *grp);
extern "C" void puller_stop (stream_chan_t *c);
extern "C" void stream_close_writer(stream_chan_t *c);   /* stream_router.c:安全关闭 writer */
extern "C" void stream_close_writer_noflush(stream_chan_t *c);

static stream_chan_t *slot(nvr_stream_mgr_t *m, int chn)
{
    if (!m || chn < 0 || chn >= NVR_MAX_CH || !m->used[chn]) return NULL;
    return &m->ch[chn];
}
static stream_pull_t *pull_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
}

extern "C" void stream_close_writer_noflush(stream_chan_t *c);

extern "C" rsdk_err_t nvr_stream_mgr_init(const nvr_stream_mgr_cfg_t *cfg, nvr_stream_mgr_t **out)
{
    if (!cfg || !out) return RSDK_E_PARAM;
    nvr_stream_mgr_t *m = (nvr_stream_mgr_t *)calloc(1, sizeof(*m));
    if (!m) return RSDK_E_NOSPACE;
    m->cfg = *cfg;
    if (m->cfg.conn_timeout <= 0) m->cfg.conn_timeout = 5;
    if (m->cfg.rx_timeout   <= 0) m->cfg.rx_timeout   = 10;
    if (stream_record_worker_start(m) != 0) {
        free(m);
        return RSDK_E_IO;
    }
    *out = m;
    return RSDK_OK;
}

extern "C" void nvr_stream_mgr_deinit(nvr_stream_mgr_t *m)
{
    if (!m) return;
    nvr_stream_stop_all(m);
    stream_record_worker_stop();
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
    s->pmain.h264_prev_fn = -1; s->psub.h264_prev_fn = -1;
    s->pmain.conn_gen = 1; s->psub.conn_gen = 1;
    s->pmain.rec_last_gen = 1; s->psub.rec_last_gen = 1;
    s->pmain.rec_state = STREAM_REC_WAIT_IDR; s->psub.rec_state = STREAM_REC_WAIT_IDR;
    s->rec_main_on = 1;
    s->rec_sub_on  = 1;
    stream_record_q_init(&s->pmain.rec_q);
    stream_record_q_init(&s->psub.rec_q);
    s->cfg.over_tcp = 1;              /* 与 puller 强制 TCP 一致 */
    s->live_state = STREAM_LIVE_IDLE;
    stream_live_q_init(&s->live_q);
    /* cfg.url(若给)按 cfg.stream 落到对应路;通常由 channel 层随后 set_url 提供主/子两条。 */
    if (c->url[0]) snprintf(pull_of(s, c->stream)->url, sizeof(s->pmain.url), "%s", c->url);
    m->used[c->chn] = 1;
    return RSDK_OK;
}

/* channel 层解析出某码流 URL → 提供给 streaming;通道已 active 则立即起该路 puller。 */
extern "C" rsdk_err_t nvr_stream_set_url(nvr_stream_mgr_t *m, int chn, int stream, const char *url)
{
    stream_chan_t *c = slot(m, chn);
    if (!c || !url || !url[0]) {
        NVR_LOGW("stream", "set_url ch%d stream=%d 拒绝: used=%d slot=%p url=%s",
                 chn, stream,
                 (m && chn >= 0 && chn < NVR_MAX_CH) ? m->used[chn] : -1,
                 (void *)c, url ? url : "(null)");
        return RSDK_E_PARAM;
    }
    stream_pull_t *p = pull_of(c, stream);
    if (strcmp(p->url, url) == 0 && p->puller) return RSDK_OK;   /* 同 URL 且在跑 */
    if (p->puller) stream_pull_stop(p);
    snprintf(p->url, sizeof(p->url), "%s", url);
    NVR_LOGI("stream", "set_url ch%d stream=%d active=%d → %s url=%s",
             chn, stream, m->active[chn],
             m->active[chn] ? "起puller" : "仅存URL(未active)", url);
    if (m->active[chn]) stream_pull_start(p, m->cfg.conn_timeout, m->cfg.rx_timeout);
    return RSDK_OK;
}

/* 显示门控(preview 驱动):设当前显示格。win>=0 开解码(用 decode_stream 那路),win<0 关解码。 */
extern "C" rsdk_err_t nvr_stream_set_display(nvr_stream_mgr_t *m, int chn, int win)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    /* ★ 不在命令/preview 线程直接开关解码器(会与 puller 线程 mhal_vdec_send 并发 → use-after-free)。
     * 只改目标 + 置脏,由 puller 线程在自己线程内兑现。变化才置脏,避免同格重复重开(黑闪)。 */
    if (c->show_win != win) { c->show_win = win; c->decode_dirty = 1; }
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
    /* 换解码源同样交由 puller 线程重开(见 nvr_stream_set_display 注释)。 */
    if (c->show_win >= 0) c->decode_dirty = 1;
    return RSDK_OK;
}

/* 强制 puller 重发 mhal_vout_bind(show_win)——即使窗口号未变(vdec_win==show_win)。
 * 用于自由窗(bind_rect)↔宫格切换:HAL 窗已被 unbind(visible=0),但流层 vdec_win 仍停在
 * 同一格号,相等守卫会短路不重绑 → 窗口永隐。置 vout_rebind + decode_dirty 唤醒 puller 兑现。 */
extern "C" rsdk_err_t nvr_stream_force_rebind(nvr_stream_mgr_t *m, int chn)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    if (c->show_win >= 0) { c->vout_rebind = 1; c->decode_dirty = 1; }
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
    int cnt = 0;
    for (int i = 0; i < NVR_MAX_CH; i++)
        if (m->used[i]) { nvr_stream_start(m, i); cnt++; }
    NVR_LOGI("stream", "start_all: 激活 %d 个已注册通道", cnt);
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
 * event_id=0 清除标签;否则打标 + 写内联 EVENT 记录(供 queryEventList/scan)。end 为事件窗口末(自动清)。
 * 若 event_arm:启动事件片段(预录 flush + 开写盘)。 */
extern "C" rsdk_err_t nvr_stream_set_event(nvr_stream_mgr_t *m, int chn, uint64_t event_id,
                                           int rectype, uint32_t start, uint32_t end, int cloud)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    c->pend_event_rectype = (uint8_t)rectype;
    c->pend_event_cloud   = (uint8_t)(cloud ? 1 : 0);
    c->pend_event_start   = start;
    c->pend_event_end     = end;
    c->pend_event_id      = event_id;   /* 最后置 id(puller 见 pend!=applied 触发应用) */
    if (event_id && c->event_arm && !c->cfg.record) {
        if (!c->event_clip) {
            c->pmain.pre_flushed = 0;
            c->psub.pre_flushed = 0;
        }
        c->event_clip = 1;
    } else if (!event_id && c->event_arm) {
        /* 显式清事件:片段收尾由 puller 在写路径关 writer;此处仅允许自然过期 */
    }
    stream_rec_mask_poke(c);
    return RSDK_OK;
}

extern "C" rsdk_err_t nvr_stream_set_record_mask(nvr_stream_mgr_t *m, int chn, int main_on, int sub_on)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    c->rec_main_on = main_on ? 1 : 0;
    c->rec_sub_on  = sub_on ? 1 : 0;
    if (!c->rec_main_on) c->rec_main_close_pend = 1;
    if (!c->rec_sub_on)  c->rec_sub_close_pend  = 1;
    if (c->cfg.record && m->cfg.group) {
        stream_open_writer(c, m->cfg.group);
    }
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
        c->event_arm = 0;             /* 连续录像优先,退出仅事件待命 */
        c->event_clip = 0;
        c->pmain.pre_flushed = 0;
        c->psub.pre_flushed = 0;
        if (m->cfg.group &&
            ((c->rec_main_on && !c->writer_main) || (c->rec_sub_on && !c->writer_sub)))
            stream_open_writer(c, m->cfg.group);
        c->rec_gated_main = 0; c->rec_gated_sub = 0;
    }
    stream_rec_mask_poke(c);
    return RSDK_OK;
}

/* 仅事件待命:arm=1 时主+子双路预录;触发 set_event 后双轨写盘至 post 窗结束。与连续录像互斥。
 * 预录环由各路 puller 按 pre_record_s 懒分配(避免命令线程与写路径并发 free)。 */
extern "C" rsdk_err_t nvr_stream_set_event_arm(nvr_stream_mgr_t *m, int chn, int arm, int pre_s)
{
    stream_chan_t *c = slot(m, chn);
    if (!c) return RSDK_E_NOTFOUND;
    if (pre_s < 0) pre_s = 0;
    if (pre_s > 30) pre_s = 30;
    c->pre_record_s = pre_s;
    if (arm && !c->cfg.record) {
        c->event_arm = 1;
    } else {
        c->event_arm = 0;
        /* 环释放交 puller:见 stream_route_video 在 !event_arm 时 pre_free */
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
        if (m->used[i] && (m->ch[i].writer_main || m->ch[i].writer_sub) &&
            (m->ch[i].cfg.record || m->ch[i].event_clip))
            mask |= (1u << i);  /* 连续开或事件片段中=录像中 */
    return mask;
}

extern "C" void nvr_stream_set_lp_poke(nvr_stream_mgr_t *m, void (*poke)(void *user), void *user)
{
    if (!m) return;
    m->lp_poke      = poke;
    m->lp_poke_user = user;
    m->rec_mask_last = nvr_stream_recording_mask(m);
}

/* 录像位图相对上次 poke 有变 → 唤醒 GUI_longPolling。 */
extern "C" void stream_rec_mask_poke(stream_chan_t *c)
{
    nvr_stream_mgr_t *m;
    uint32_t now;
    if (!c || !(m = c->mgr) || !m->lp_poke) return;
    now = nvr_stream_recording_mask(m);
    if (now == m->rec_mask_last) return;
    m->rec_mask_last = now;
    m->lp_poke(m->lp_poke_user);
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
        if (group && c->cfg.record &&
            ((c->rec_main_on && !c->writer_main) || (c->rec_sub_on && !c->writer_sub))) {
            stream_open_writer(c, group); opened++;
        }
    }
    NVR_LOGI("stream", "set_group: 更新录像盘组, 补开 %d 路 writer", opened);
    if (m->lp_poke) {
        uint32_t now = nvr_stream_recording_mask(m);
        if (now != m->rec_mask_last) {
            m->rec_mask_last = now;
            m->lp_poke(m->lp_poke_user);
        }
    }
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
        if (c->cfg.record || c->event_clip) was |= (1u << i);
        c->cfg.record = 0;            /* puller 下一帧起跳过写盘 */
        c->event_arm = 0;
        c->event_clip = 0;
    }
    usleep(150 * 1000);               /* drain:等已越过 record 检查的在途 write_frame 完成 */
    int closed = 0;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        if (m->ch[i].writer_main || m->ch[i].writer_sub) { stream_close_writer(&m->ch[i]); closed++; }
    }
    NVR_LOGW("stream", "格式化暂停写盘:置 record=0 + 关闭 %d 路 writer(盘静默)", closed);
    if (m->lp_poke) {
        m->rec_mask_last = nvr_stream_recording_mask(m);
        m->lp_poke(m->lp_poke_user);
    }
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
    if (m->lp_poke) {
        uint32_t now = nvr_stream_recording_mask(m);
        if (now != m->rec_mask_last) {
            m->rec_mask_last = now;
            m->lp_poke(m->lp_poke_user);
        }
    }
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
