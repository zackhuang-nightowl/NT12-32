/* stream_record_worker.h — Record Worker: puller 只 push, rsdk 写盘在此线程 */
#ifndef STREAM_RECORD_WORKER_H
#define STREAM_RECORD_WORKER_H

struct nvr_stream_mgr;

#ifdef __cplusplus
extern "C" {
#endif

int  stream_record_worker_start(struct nvr_stream_mgr *mgr);
void stream_record_worker_stop(void);
void stream_record_worker_poke(void);
/* 关 writer / 停通道前: 阻塞直到全部 RecordQueue 排空(或超时 ms) */
void stream_record_worker_flush_sync(int timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
