/* stream_record_worker.c — Record Worker: 唯一线程调用 rsdk_rec_write_frame */
#include "stream_record_worker.h"
#include "stream_internal.h"
#include "stream_hub.h"
#include "nvr_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define REC_WORKER_BYTE_BUDGET  (512 * 1024)  /* 每路每轮最多写 512KB */
#define REC_WORKER_IDLE_MS      50
#define REC_WORKER_FLUSH_MS     5000
#define REC_SEG_SECONDS         60             /* 定时切片目标时长(秒); 到点后在下个 IDR 切新段 */
#define REC_DATASYNC_SECONDS    1              /* 周期 fdatasync 间隔(秒): 掉电丢失窗口上界 */

static struct {
    pthread_t       th;
    volatile int    running;
    nvr_stream_mgr_t *mgr;
    pthread_mutex_t wake_mtx;
    pthread_cond_t  wake_cv;
    volatile int    flush_req;
    volatile int    flush_done;
    int             rr;           /* 公平轮询起点 */
    time_t          last_sync;    /* 上次周期 fsync 的墙钟秒 */
} g_w;

#define STREAM_REC_GAP_TYPE_MASK  0x80000000u

static rsdk_writer_t *writer_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? c->writer_sub : c->writer_main;
}

static stream_pull_t *pull_of(stream_chan_t *c, int stream)
{
    return (stream == NVR_STREAM_SUB) ? &c->psub : &c->pmain;
}

static int worker_drain_pull(stream_chan_t *c, stream_pull_t *p, int byte_budget);

static void worker_apply_event_tags(stream_chan_t *c)
{
    uint64_t pend = c->pend_event_id;
    if (pend != c->applied_event_id) {
        if (c->writer_main) {
            rsdk_rec_set_event(c->writer_main, pend);
            if (pend)
                rsdk_rec_mark_event(c->writer_main, pend, c->pend_event_rectype,
                                    c->pend_event_start, c->pend_event_end,
                                    (uint32_t)(1u << (c->pend_event_rectype & 31)), 0);
        }
        if (c->writer_sub) rsdk_rec_set_event(c->writer_sub, pend);
        c->applied_event_id = pend;
    }
    /* 事件窗结束关 writer 由 puller 调 stream_close_writer(flush+close),避免 worker 重复关 */
}

static void worker_mark_gap(stream_chan_t *c, stream_pull_t *p, const char *reason)
{
    rsdk_writer_t *wrec = writer_of(c, p->stream);
    if (!wrec) return;
    rsdk_rec_mark_event(wrec, 0, RSDK_REC_CONTINUOUS, (uint32_t)time(NULL), 0,
                          STREAM_REC_GAP_TYPE_MASK, 0);
    NVR_LOGW("record", "ch%d[%s] gap: %s gen=%u",
             c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
             reason ? reason : "disc", p->conn_gen);
}

static void worker_handle_gap(stream_chan_t *c, stream_pull_t *p)
{
    (void)c;
    if (p->rec_gap_pending && p->rec_state == STREAM_REC_RECORDING)
        worker_mark_gap(c, p, "disc_or_gen");
    if (p->conn_gen != p->rec_last_gen)
        p->rec_last_gen = p->conn_gen;
    p->rec_gap_pending = 0;
}

static int worker_write_slot(stream_chan_t *c, stream_pull_t *p,
                             const stream_record_slot_t *s)
{
    rsdk_writer_t *wrec;
    rsdk_frame_t f;
    uint32_t wt;

    if (!s || !s->data || s->len == 0) return 0;
    worker_apply_event_tags(c);
    worker_handle_gap(c, p);

    /* 采集时刻优先; 兜底当前墙钟(仅极早期无 wall 的帧)。录像时间轴用它, 不受写盘滞后影响。 */
    wt = s->wall_time ? s->wall_time : (uint32_t)time(NULL);

    if (s->media == STREAM_REC_MEDIA_AUDIO) {
        if (!c->writer_main || !c->rec_gated_main) return 0;
        wrec = c->writer_main;
        memset(&f, 0, sizeof(f));
        f.chn = (uint16_t)c->cfg.chn;
        f.stream = 2;
        f.codec = RSDK_CODEC_AAC;
        f.frame_type = RSDK_FRAME_AUDIO;
        f.pts = (uint64_t)s->ts;
        f.wall_time = (uint64_t)wt;
        f.data = s->data;
        f.len = s->len;
        return rsdk_rec_write_frame(wrec, &f) == RSDK_OK ? (int)s->len : 0;
    }

    wrec = writer_of(c, p->stream);
    if (!wrec || s->is_param) return 0;
    if (p->rec_state == STREAM_REC_WAIT_IDR) {
        if (!s->is_key) return 0;
        p->rec_state = STREAM_REC_RECORDING;
        p->rec_seg_start_wall = wt;                 /* 新段起始(用于定时切片计时) */
        if (p->stream == NVR_STREAM_SUB) c->rec_gated_sub = 1;
        else c->rec_gated_main = 1;
    }
    /* ★ 定时切片(IDR 对齐): 到达目标时长后, 在下一个关键帧处切新段 → 新段从 IDR 起, 回放段界无缝。 */
    if (s->is_key && p->rec_seg_start_wall &&
        (uint32_t)(wt - p->rec_seg_start_wall) >= REC_SEG_SECONDS) {
        if (rsdk_rec_rotate(wrec) == RSDK_OK) p->rec_seg_start_wall = wt;
    }
    memset(&f, 0, sizeof(f));
    f.chn = (uint16_t)c->cfg.chn;
    f.stream = (uint8_t)p->stream;
    f.codec = (uint8_t)p->codec;
    f.frame_type = s->frame_type;
    f.pts = (uint64_t)s->ts;
    f.wall_time = (uint64_t)wt;
    f.data = s->data;
    f.len = s->len;
    return rsdk_rec_write_frame(wrec, &f) == RSDK_OK ? (int)s->len : 0;
}

/* 消费一路队列至多 byte_budget 字节; 返回实际写字节 */
static int worker_drain_pull(stream_chan_t *c, stream_pull_t *p, int byte_budget)
{
    stream_record_q_t *q = &p->rec_q;
    int written = 0;
    rsdk_writer_t *w;

    if (!c || !p || byte_budget <= 0) return 0;
    w = writer_of(c, p->stream);
    if (!w && !c->writer_main) {
        stream_record_q_flush(q);
        return 0;
    }

    if (q->stall_logged && q->count >= STREAM_RECORD_Q_HIGH_WM)
        NVR_LOGW("record", "ch%d[%s] RecordQueue 高水位 %d/%d (stall=%u)",
                 c->cfg.chn, p->stream == NVR_STREAM_SUB ? "子" : "主",
                 q->count, STREAM_RECORD_Q_CAP, q->stall_cnt);

    for (;;) {
        const stream_record_slot_t *s;
        int n;
        stream_record_q_lock(q);
        if (q->count <= 0 || written >= byte_budget) {
            stream_record_q_unlock(q);
            break;
        }
        s = stream_record_q_peek(q);
        if (!s || !s->data || s->len == 0) {
            stream_record_q_pop_locked(q);
            stream_record_q_unlock(q);
            continue;
        }
        if (written > 0 && written + (int)s->len > byte_budget) {
            stream_record_q_unlock(q);
            break;
        }
        /* 拷贝槽内容后解锁再写盘(缩短持锁) */
        {
            stream_record_slot_t local = *s;
            uint8_t *copy = (uint8_t *)malloc(local.len);
            if (!copy) { stream_record_q_unlock(q); break; }
            memcpy(copy, s->data, local.len);
            local.data = copy;
            stream_record_q_pop_locked(q);
            stream_record_q_unlock(q);
            n = worker_write_slot(c, p, &local);
            free(copy);
            if (n > 0) written += n;
        }
    }
    return written;
}

/* 周期把已写数据 fdatasync 到介质: 把掉电丢失窗口压到 REC_DATASYNC_SECONDS 内。
 * best-effort(与现有 worker 读 writer 同生命周期假设一致; 彻底消除 writer UAF 需 writer 归属重构)。 */
static void worker_periodic_sync(nvr_stream_mgr_t *m)
{
    time_t now = time(NULL);
    if (now - g_w.last_sync < REC_DATASYNC_SECONDS) return;
    g_w.last_sync = now;
    for (int i = 0; i < NVR_MAX_CH; i++) {
        stream_chan_t *c;
        if (!m->used[i]) continue;
        c = &m->ch[i];
        if (c->writer_main) rsdk_rec_datasync(c->writer_main);
        if (c->writer_sub)  rsdk_rec_datasync(c->writer_sub);
    }
}

static int any_queue_pending(nvr_stream_mgr_t *m)
{
    int i;
    if (!m) return 0;
    for (i = 0; i < NVR_MAX_CH; i++) {
        if (!m->used[i]) continue;
        if (stream_record_q_count(&m->ch[i].pmain.rec_q) > 0) return 1;
        if (stream_record_q_count(&m->ch[i].psub.rec_q) > 0) return 1;
    }
    return 0;
}

static void worker_round(nvr_stream_mgr_t *m)
{
    int i, n, did = 0;
    if (!m) return;
    n = NVR_MAX_CH * 2;
    for (i = 0; i < n; i++) {
        int idx = (g_w.rr + i) % n;
        int chn = idx / 2;
        int stream = idx % 2;
        stream_chan_t *c;
        stream_pull_t *p;
        int w;
        if (!m->used[chn]) continue;
        c = &m->ch[chn];
        p = pull_of(c, stream);
        w = worker_drain_pull(c, p, REC_WORKER_BYTE_BUDGET);
        if (w > 0) did = 1;
    }
    g_w.rr = (g_w.rr + 1) % (NVR_MAX_CH * 2);
    if (!did && g_w.flush_req) {
        if (!any_queue_pending(m)) {
            g_w.flush_done = 1;
            g_w.flush_req = 0;
            pthread_cond_broadcast(&g_w.wake_cv);
        }
    }
}

static void *record_worker_thread(void *arg)
{
    (void)arg;
    NVR_LOGI("record", "Record Worker 启动(单线程串行 rsdk 写盘)");
    while (g_w.running) {
        worker_round(g_w.mgr);
        worker_periodic_sync(g_w.mgr);   /* 周期 fdatasync: 掉电窗口 ≤ REC_DATASYNC_SECONDS */
        if (!g_w.running) break;
        if (!any_queue_pending(g_w.mgr) && !g_w.flush_req) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (long)REC_WORKER_IDLE_MS * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            pthread_mutex_lock(&g_w.wake_mtx);
            if (g_w.running && !g_w.flush_req && !any_queue_pending(g_w.mgr))
                pthread_cond_timedwait(&g_w.wake_cv, &g_w.wake_mtx, &ts);
            pthread_mutex_unlock(&g_w.wake_mtx);
        }
    }
    NVR_LOGI("record", "Record Worker 退出");
    return NULL;
}

void stream_record_worker_poke(void)
{
    if (!g_w.running) return;
    pthread_cond_signal(&g_w.wake_cv);
}

int stream_record_worker_start(nvr_stream_mgr_t *mgr)
{
    if (g_w.running) { g_w.mgr = mgr; return 0; }
    if (!mgr) return -1;
    memset(&g_w, 0, sizeof(g_w));
    g_w.mgr = mgr;
    pthread_mutex_init(&g_w.wake_mtx, NULL);
    pthread_cond_init(&g_w.wake_cv, NULL);
    g_w.running = 1;
    if (pthread_create(&g_w.th, NULL, record_worker_thread, NULL) != 0) {
        g_w.running = 0;
        return -1;
    }
    return 0;
}

void stream_record_worker_stop(void)
{
    if (!g_w.running) return;
    g_w.running = 0;
    pthread_cond_signal(&g_w.wake_cv);
    pthread_join(g_w.th, NULL);
    pthread_mutex_destroy(&g_w.wake_mtx);
    pthread_cond_destroy(&g_w.wake_cv);
    g_w.mgr = NULL;
}

void stream_record_worker_flush_sync(int timeout_ms)
{
    int ms = timeout_ms > 0 ? timeout_ms : REC_WORKER_FLUSH_MS;
    if (!g_w.running) return;
    if (!any_queue_pending(g_w.mgr)) return;
    pthread_mutex_lock(&g_w.wake_mtx);
    g_w.flush_req = 1;
    g_w.flush_done = 0;
    pthread_cond_signal(&g_w.wake_cv);
    while (g_w.flush_req && ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_w.wake_cv, &g_w.wake_mtx, &ts);
        ms -= 100;
        worker_round(g_w.mgr);
    }
    g_w.flush_req = 0;
    pthread_mutex_unlock(&g_w.wake_mtx);
}
