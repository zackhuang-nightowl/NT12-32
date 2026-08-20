#include "stream_hub.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t stream_hub_mono_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* ---- Live queue ---- */

void stream_live_q_init(stream_live_q_t *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
}

void stream_live_q_flush(stream_live_q_t *q)
{
    int i;
    if (!q) return;
    for (i = 0; i < STREAM_LIVE_Q_CAP; i++) {
        free(q->slot[i].data);
        q->slot[i].data = NULL;
        q->slot[i].len = 0;
    }
    q->head = 0;
    q->count = 0;
}

static void live_drop_oldest(stream_live_q_t *q)
{
    int oldest = (q->head - q->count + STREAM_LIVE_Q_CAP) % STREAM_LIVE_Q_CAP;
    free(q->slot[oldest].data);
    q->slot[oldest].data = NULL;
    q->slot[oldest].len = 0;
    q->count--;
    q->drop_oldest++;
}

static int live_over_latency(const stream_live_q_t *q, uint64_t mono_ms)
{
    int oldest;
    const stream_live_slot_t *s;
    if (!q || q->count <= 0 || mono_ms == 0) return 0;
    oldest = (q->head - q->count + STREAM_LIVE_Q_CAP) % STREAM_LIVE_Q_CAP;
    s = &q->slot[oldest];
    if (s->enqueue_ms == 0) return 0;
    return (mono_ms > s->enqueue_ms &&
            (mono_ms - s->enqueue_ms) > STREAM_LIVE_MAX_LATENCY_MS) ? 1 : 0;
}

int stream_live_q_push(stream_live_q_t *q, const uint8_t *data, uint32_t len,
                       uint32_t ts, int is_key, uint64_t mono_ms)
{
    stream_live_slot_t *s;
    uint8_t *copy;
    int dropped = 0;
    if (!q || !data || len == 0) return 0;
    copy = (uint8_t *)malloc(len);
    if (!copy) return 0;
    memcpy(copy, data, len);

    while (q->count == STREAM_LIVE_Q_CAP || live_over_latency(q, mono_ms)) {
        if (q->count <= 0) break;
        live_drop_oldest(q);
        dropped = 1;
    }

    s = &q->slot[q->head];
    s->data = copy;
    s->len = len;
    s->ts = ts;
    s->enqueue_ms = mono_ms;
    s->is_key = is_key ? 1 : 0;
    q->head = (q->head + 1) % STREAM_LIVE_Q_CAP;
    q->count++;
    return dropped;
}

const stream_live_slot_t *stream_live_q_peek(const stream_live_q_t *q)
{
    int oldest;
    if (!q || q->count <= 0) return NULL;
    oldest = (q->head - q->count + STREAM_LIVE_Q_CAP) % STREAM_LIVE_Q_CAP;
    return &q->slot[oldest];
}

void stream_live_q_pop(stream_live_q_t *q)
{
    int oldest;
    if (!q || q->count <= 0) return;
    oldest = (q->head - q->count + STREAM_LIVE_Q_CAP) % STREAM_LIVE_Q_CAP;
    free(q->slot[oldest].data);
    q->slot[oldest].data = NULL;
    q->slot[oldest].len = 0;
    q->count--;
}

/* ---- Record queue (thread-safe: puller push / worker pop) ---- */

void stream_record_q_init(stream_record_q_t *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mtx, NULL);
}

void stream_record_q_flush(stream_record_q_t *q)
{
    int i;
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    for (i = 0; i < STREAM_RECORD_Q_CAP; i++) {
        free(q->slot[i].data);
        q->slot[i].data = NULL;
        q->slot[i].len = 0;
    }
    q->head = 0;
    q->count = 0;
    q->stall_logged = 0;
    pthread_mutex_unlock(&q->mtx);
}

int stream_record_q_count(const stream_record_q_t *q)
{
    int n;
    if (!q) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&q->mtx);
    n = q->count;
    pthread_mutex_unlock((pthread_mutex_t *)&q->mtx);
    return n;
}

int stream_record_q_push(stream_record_q_t *q, const uint8_t *data, uint32_t len,
                         uint32_t ts, int is_key, int is_param,
                         uint8_t frame_type, uint8_t codec, uint8_t media)
{
    stream_record_slot_t *s;
    uint8_t *copy;
    int rc = 0;
    if (!q || !data || len == 0) return 0;
    copy = (uint8_t *)malloc(len);
    if (!copy) return -1;

    pthread_mutex_lock(&q->mtx);
    if (q->count >= STREAM_RECORD_Q_CAP) {
        q->stall_cnt++;
        rc = -1;
    } else {
        memcpy(copy, data, len);
        s = &q->slot[q->head];
        s->data = copy;
        s->len = len;
        s->ts = ts;
        s->is_key = is_key ? 1 : 0;
        s->is_param = is_param ? 1 : 0;
        s->frame_type = frame_type;
        s->codec = codec;
        s->media = media;
        q->head = (q->head + 1) % STREAM_RECORD_Q_CAP;
        q->count++;
        if (q->count >= STREAM_RECORD_Q_HIGH_WM && !q->stall_logged)
            q->stall_logged = 1;
        copy = NULL;
    }
    pthread_mutex_unlock(&q->mtx);
    if (copy) free(copy);
    return rc;
}

const stream_record_slot_t *stream_record_q_peek(const stream_record_q_t *q)
{
    int oldest;
    if (!q || q->count <= 0) return NULL;
    oldest = (q->head - q->count + STREAM_RECORD_Q_CAP) % STREAM_RECORD_Q_CAP;
    return &q->slot[oldest];
}

/* worker 专用: 须在持有 q->mtx 时 peek, pop 释放 data */
void stream_record_q_pop_locked(stream_record_q_t *q)
{
    int oldest;
    if (!q || q->count <= 0) return;
    oldest = (q->head - q->count + STREAM_RECORD_Q_CAP) % STREAM_RECORD_Q_CAP;
    free(q->slot[oldest].data);
    q->slot[oldest].data = NULL;
    q->slot[oldest].len = 0;
    q->count--;
    if (q->count < STREAM_RECORD_Q_HIGH_WM / 2)
        q->stall_logged = 0;
}

void stream_record_q_pop(stream_record_q_t *q)
{
    if (!q) return;
    pthread_mutex_lock(&q->mtx);
    stream_record_q_pop_locked(q);
    pthread_mutex_unlock(&q->mtx);
}

void stream_record_q_lock(stream_record_q_t *q)   { if (q) pthread_mutex_lock(&q->mtx); }
void stream_record_q_unlock(stream_record_q_t *q) { if (q) pthread_mutex_unlock(&q->mtx); }
