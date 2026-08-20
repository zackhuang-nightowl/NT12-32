/* stream_hub.h — Frame Hub: Live 小队列 + Record 大队列 + 状态机类型
 *
 * 架构: CI → HUB → { BS(旁路), RQ, LQ }；BS 不参与帧转发。 */
#ifndef STREAM_HUB_H
#define STREAM_HUB_H

#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Live 状态机(per channel, 仅 decode_stream) ---- */
typedef enum {
    STREAM_LIVE_IDLE = 0,
    STREAM_LIVE_WAIT_IDR,
    STREAM_LIVE_SYNCED,
    STREAM_LIVE_RESYNC
} stream_live_state_t;

#define STREAM_LIVE_Q_CAP          8
#define STREAM_LIVE_MAX_LATENCY_MS 500   /* 双阈值: 帧数 + 积压毫秒 */

typedef struct {
    uint8_t  *data;
    uint32_t  len;
    uint32_t  ts;
    uint64_t  enqueue_ms;     /* CLOCK_MONOTONIC ms, 用于 latency 阈值 */
    uint8_t   is_key;
} stream_live_slot_t;

typedef struct {
    stream_live_slot_t slot[STREAM_LIVE_Q_CAP];
    int head;
    int count;
    unsigned drop_oldest;
} stream_live_q_t;

void stream_live_q_init(stream_live_q_t *q);
void stream_live_q_flush(stream_live_q_t *q);
/* 拷贝入队; 超帧数或超 latency 则丢最旧。返回 1=发生丢旧。 */
int  stream_live_q_push(stream_live_q_t *q, const uint8_t *data, uint32_t len,
                        uint32_t ts, int is_key, uint64_t mono_ms);
const stream_live_slot_t *stream_live_q_peek(const stream_live_q_t *q);
void stream_live_q_pop(stream_live_q_t *q);

/* ---- Recorder 状态机(per pull stream) ---- */
typedef enum {
    STREAM_REC_WAIT_IDR = 0,   /* 等首 IDR 起录 */
    STREAM_REC_RECORDING       /* 连续写, disc/gen 只打 gap 标记 */
} stream_rec_state_t;

#define STREAM_RECORD_Q_CAP      96
#define STREAM_RECORD_Q_HIGH_WM  64   /* 超过则 stall 告警(不丢帧) */

#define STREAM_REC_MEDIA_VIDEO   0
#define STREAM_REC_MEDIA_AUDIO   1

typedef struct {
    uint8_t  *data;
    uint32_t  len;
    uint32_t  ts;
    uint8_t   is_key;
    uint8_t   is_param;
    uint8_t   frame_type;
    uint8_t   codec;
    uint8_t   media;          /* STREAM_REC_MEDIA_* */
} stream_record_slot_t;

typedef struct {
    stream_record_slot_t slot[STREAM_RECORD_Q_CAP];
    int head;
    int count;
    unsigned stall_cnt;
    int stall_logged;
    pthread_mutex_t mtx;
} stream_record_q_t;

void stream_record_q_init(stream_record_q_t *q);
void stream_record_q_flush(stream_record_q_t *q);
/* 拷贝入队; 满返回 -1(调用方须先 drain, 不丢帧)。 */
int  stream_record_q_push(stream_record_q_t *q, const uint8_t *data, uint32_t len,
                          uint32_t ts, int is_key, int is_param,
                          uint8_t frame_type, uint8_t codec, uint8_t media);
const stream_record_slot_t *stream_record_q_peek(const stream_record_q_t *q);
void stream_record_q_pop(stream_record_q_t *q);
int  stream_record_q_count(const stream_record_q_t *q);
/* worker 内部: 已持锁 peek 后 pop */
void stream_record_q_pop_locked(stream_record_q_t *q);
void stream_record_q_lock(stream_record_q_t *q);
void stream_record_q_unlock(stream_record_q_t *q);

uint64_t stream_hub_mono_ms(void);

#ifdef __cplusplus
}
#endif
#endif
