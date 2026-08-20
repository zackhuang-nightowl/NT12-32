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

/* ---- Record queue ---- */

void stream_record_q_init(stream_record_q_t *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
}

void stream_record_q_flush(stream_record_q_t *q)
{
    int i;
    if (!q) return;
    for (i = 0; i < STREAM_RECORD_Q_CAP; i++) {
        free(q->slot[i].data);
        q->slot[i].data = NULL;
        q->slot[i].len = 0;
    }
    q->head = 0;
    q->count = 0;
    q->stall_logged = 0;
}

int stream_record_q_count(const stream_record_q_t *q)
{
    return q ? q->count : 0;
}

int stream_record_q_push(stream_record_q_t *q, const uint8_t *data, uint32_t len,
                         uint32_t ts, int is_key, int is_param,
                         uint8_t frame_type, uint8_t codec)
{
    stream_record_slot_t *s;
    uint8_t *copy;
    if (!q || !data || len == 0) return 0;
    if (q->count >= STREAM_RECORD_Q_CAP) {
        q->stall_cnt++;
        return -1;
    }
    copy = (uint8_t *)malloc(len);
    if (!copy) return -1;
    memcpy(copy, data, len);

    s = &q->slot[q->head];
    s->data = copy;
    s->len = len;
    s->ts = ts;
    s->is_key = is_key ? 1 : 0;
    s->is_param = is_param ? 1 : 0;
    s->frame_type = frame_type;
    s->codec = codec;
    q->head = (q->head + 1) % STREAM_RECORD_Q_CAP;
    q->count++;

    if (q->count >= STREAM_RECORD_Q_HIGH_WM && !q->stall_logged) {
        q->stall_logged = 1;
    }
    return 0;
}

const stream_record_slot_t *stream_record_q_peek(const stream_record_q_t *q)
{
    int oldest;
    if (!q || q->count <= 0) return NULL;
    oldest = (q->head - q->count + STREAM_RECORD_Q_CAP) % STREAM_RECORD_Q_CAP;
    return &q->slot[oldest];
}

void stream_record_q_pop(stream_record_q_t *q)
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
