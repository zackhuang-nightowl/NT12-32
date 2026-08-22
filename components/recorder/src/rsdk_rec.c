/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像写入(设计 §3/§7).
 * 约定: 一个 RecSegment 落在单个 chunk 内; 帧不够放则闭合当前段、开新段(新chunk),
 *       从而回放只需在一个 chunk 内顺序走帧, 无需 chunk 链。 */
#include "rsdk_rec.h"
#include "rsdk_balance.h"
#include "rsdk_index.h"
#include "rsdk_evtidx.h"
#include "rsdk_crypto.h"
#include "rsdk_util.h"
#include <stdlib.h>
#include <string.h>

struct rsdk_writer {
    rsdk_dev_t *d; rsdk_group_t *group; int chn, rectype;
    int      stream;             /* 0主/1子(写入索引 slot.stream) */
    uint32_t seg_id;
    uint64_t chunk, base_off; uint32_t cur_off;   /* 当前段所在 chunk + 段内偏移 */
    uint32_t frame_seq, frame_count;
    uint64_t total_bytes;
    uint32_t start_time, last_time; uint32_t end_off;
    int      open_written;
    uint64_t cur_event_id;   /* 连续轨事件标签:>0 则后续帧头打此 event_id;0=无 */
    uint8_t *pbuf; size_t pcap;
    rsdk_kf_entry_t *kf; uint32_t kf_count, kf_cap;  /* 本段关键帧表(段闭合写入 chunk) */
    rsdk_reclaim_cb reclaim; void *reclaim_user;
};

void rsdk_rec_set_reclaim(rsdk_writer_t *w, rsdk_reclaim_cb cb, void *user) {
    if (w) { w->reclaim = cb; w->reclaim_user = user; }
}

/* 记录一个 IDR 的 (墙钟, chunk内偏移) 到本段关键帧表(供段闭合时落 RK_KEYIDX)。 */
static void kf_record(rsdk_writer_t *w, uint32_t wall, uint32_t chunk_off) {
    if (w->kf_count >= 4096) return;             /* 上限兜底(超长段) */
    if (w->kf_count == w->kf_cap) {
        uint32_t nc = w->kf_cap ? w->kf_cap * 2 : 64;
        rsdk_kf_entry_t *n = realloc(w->kf, nc * sizeof *n);
        if (!n) return;
        w->kf = n; w->kf_cap = nc;
    }
    w->kf[w->kf_count].wall_time = wall;
    w->kf[w->kf_count].chunk_off = chunk_off;
    w->kf_count++;
}

/* 段闭合前把本段关键帧表写进当前 chunk(RK_KEYIDX 记录)。放不下则跳过(回放退化为顺扫,
 * 扫盘重建仍可复原)。直接写盘、不做段轮转,避免与 finalize_seg 递归。 */
static void flush_keyframes(rsdk_writer_t *w) {
    if (!w->kf || w->kf_count == 0) return;
    uint32_t pl = w->kf_count * (uint32_t)sizeof(rsdk_kf_entry_t);
    uint64_t chunk_bytes = rsdk_dev_sb(w->d)->chunk_sectors * RSDK_SEC;
    uint32_t need = (uint32_t)rsdk_align_up(64ull + pl, RSDK_FRAME_ALIGN);
    if (w->cur_off + need > chunk_bytes) { w->kf_count = 0; return; }   /* 放不下:跳过 */
    rsdk_frame_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.magic, RSDK_FRAME_MAGIC, 8);
    h.chn = (uint16_t)w->chn; h.stream = (uint8_t)w->stream;
    h.rec_kind = RSDK_RK_KEYIDX; h.rectype = (uint8_t)w->rectype;
    h.payload_len = pl; h.seg_id = w->seg_id; h.frame_seq = w->frame_seq;
    h.wall_time = w->last_time; h.enc = 0; h.payload_crc32 = 0;
    h.hdr_crc32 = 0; h.hdr_crc32 = rsdk_crc32(&h, sizeof h);
    uint64_t off = w->base_off + w->cur_off;
    if (rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off, &h, sizeof h) == RSDK_OK)
        rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off + 64, w->kf, pl);
    w->cur_off += need;
    w->kf_count = 0;
}

/* 事件标签:置后续帧头 event_id(连续轨命中事件时打标;传 0 清除)。 */
void rsdk_rec_set_event(rsdk_writer_t *w, uint64_t event_id) {
    if (w) w->cur_event_id = event_id;
}

static uint32_t next_seg_id(rsdk_dev_t *d, int chn) {
    /* st->chn[].seg_seq 的 RMW; 调用方(start_seg)已持锁, 此处不再取。 */
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint32_t seq = ++st->chn[chn & 31].seg_seq;
    return ((uint32_t)(chn & 0xFF) << 24) | (seq & 0x00FFFFFF);
}

/* writer 元数据锁: 盘组用组共享锁(与选盘/迁移一致), 单盘用该盘自锁。递归 → 内部原语自锁可嵌套。 */
static void wlock(rsdk_writer_t *w)   { if (w->group) rsdk_group_lock(w->group); else rsdk_dev_lock(w->d); }
static void wunlock(rsdk_writer_t *w) { if (w->group) rsdk_group_unlock(w->group); else rsdk_dev_unlock(w->d); }

static rsdk_err_t start_seg(rsdk_writer_t *w) {
    /* ★ 整段"选盘+分配chunk+回收+定 seg_id"在锁内原子完成: 杜绝与并发 open/写的 chunk 双分配(R1)
     * 与 index_next/seg_seq 竞争。数据区尚未写, 锁只覆盖元数据, 不含数据 pwrite。 */
    wlock(w);
    rsdk_err_t rc = RSDK_OK;
    if (w->group) {                        /* 多盘: 每段按负载均衡选盘 */
        rsdk_dev_t *picked;
        rc = rsdk_balance_pick(w->group, w->chn, &picked);
        if (rc) { wunlock(w); return rc; }
        w->d = picked;
    }
    uint64_t chunk, off;
    rc = rsdk_dev_alloc_chunk(w->d, &chunk, &off);
    if (rc) { wunlock(w); return rc; }          /* stop 策略盘满 → NOSPACE */
    if (rsdk_dev_is_wrapped(w->d)) {            /* 覆盖模式: 回收该 chunk 上旧段索引 + 事件索引 + 元数据 */
        rsdk_index_invalidate_chunk(w->d, chunk);
        rsdk_evtidx_invalidate_chunk(w->d, chunk);   /* 事件槽随视频覆盖一并作废(全链一致) */
        if (w->reclaim) w->reclaim(w->reclaim_user, rsdk_dev_index(w->d), chunk);
    }
    w->chunk = chunk; w->base_off = off; w->cur_off = 0;
    w->seg_id = next_seg_id(w->d, w->chn);
    w->frame_seq = 0; w->frame_count = 0; w->total_bytes = 0;
    w->start_time = 0; w->last_time = 0; w->end_off = 0; w->open_written = 0;
    w->kf_count = 0;   /* 新段:清关键帧表 */
    wunlock(w);
    return RSDK_OK;
}

static void fill_slot(rsdk_writer_t *w, rsdk_index_slot_t *s, int closed) {
    memset(s, 0, sizeof *s);
    s->seg_id = w->seg_id; s->chn = w->chn; s->rectype = w->rectype;
    s->flags = closed ? RSDK_SLOT_VALID : RSDK_SLOT_OPEN;
    if (w->rectype != RSDK_REC_CONTINUOUS) s->flags |= RSDK_SLOT_EVENT;
    s->start_time = w->start_time;
    s->end_time = closed ? w->last_time : 0xFFFFFFFFu;
    s->frame_count = w->frame_count; s->total_bytes = w->total_bytes;
    s->start_disk = rsdk_dev_sb(w->d)->group_disk_index;   /* 本盘在盘组内序号 */
    s->end_disk = s->start_disk;
    s->start_chunk = w->chunk; s->start_off = 0;
    s->end_chunk = w->chunk; s->end_off = w->end_off;
    s->stream = (uint8_t)(w->stream & 0xFF);
}

static rsdk_err_t finalize_seg(rsdk_writer_t *w) {
    if (w->frame_count == 0) return RSDK_OK;
    flush_keyframes(w);   /* 段闭合:先落关键帧表到本 chunk(写自身 chunk 数据区,无需元数据锁) */
    /* 段封口: 写 VALID 槽 + 报告带宽, 与并发 index_write/balance 串行(递归锁内嵌各原语自锁)。 */
    wlock(w);
    rsdk_index_slot_t s; fill_slot(w, &s, 1);
    rsdk_err_t rc = rsdk_index_write(w->d, &s);
    /* 多盘: 段结束后向均衡层报告本段字节数, 更新 EWMA 写带宽 */
    if (w->group) rsdk_balance_report(w->group, w->d, w->total_bytes);
    wunlock(w);
    return rc;
}

rsdk_err_t rsdk_rec_open(rsdk_dev_t *d, int chn, int rectype, rsdk_writer_t **out) {
    if (!d || !out) return RSDK_E_PARAM;
    rsdk_writer_t *w = calloc(1, sizeof *w);
    if (!w) return RSDK_E_IO;
    w->d = d; w->chn = chn; w->rectype = rectype;
    rsdk_err_t rc = start_seg(w);
    if (rc) { free(w); return rc; }
    *out = w;
    return RSDK_OK;
}

rsdk_err_t rsdk_rec_open_group(rsdk_group_t *g, int chn, int rectype, rsdk_writer_t **out) {
    return rsdk_rec_open_group_stream(g, chn, rectype, 0, out);
}

rsdk_err_t rsdk_rec_open_group_stream(rsdk_group_t *g, int chn, int rectype, int stream,
                                      rsdk_writer_t **out) {
    if (!g || !out) return RSDK_E_PARAM;
    rsdk_writer_t *w = calloc(1, sizeof *w);
    if (!w) return RSDK_E_IO;
    w->group = g; w->chn = chn; w->rectype = rectype;
    w->stream = (stream < 0) ? 0 : stream;
    rsdk_err_t rc = start_seg(w);
    if (rc) { free(w); return rc; }
    *out = w;
    return RSDK_OK;
}

void rsdk_rec_set_stream(rsdk_writer_t *w, int stream) {
    if (w && w->frame_count == 0) w->stream = (stream < 0) ? 0 : stream;
}

uint32_t rsdk_rec_seg_id(rsdk_writer_t *w) { return w ? w->seg_id : 0; }
uint64_t rsdk_rec_cur_chunk(rsdk_writer_t *w) { return w ? w->chunk : 0; }

/* 当前段所在 chunk 是否已近满(≥90%)。供上层"段填满才切新段"的 IDR 对齐轮转:
 * 近满且来了 IDR 时切段 → 既填满 chunk(不浪费大 chunk),又保证新段从 IDR 起(回放段界干净)。 */
int rsdk_rec_chunk_near_full(rsdk_writer_t *w) {
    if (!w || !w->d) return 0;
    uint64_t chunk_bytes = rsdk_dev_sb(w->d)->chunk_sectors * RSDK_SEC;
    return (uint64_t)w->cur_off >= chunk_bytes - chunk_bytes / 10;   /* 剩余 <10% */
}

rsdk_err_t rsdk_rec_write_frame(rsdk_writer_t *w, const rsdk_frame_t *f) {
    if (!w || !f || !f->data || f->len == 0) return RSDK_E_PARAM;
    uint64_t chunk_bytes = rsdk_dev_sb(w->d)->chunk_sectors * RSDK_SEC;
    uint32_t need = (uint32_t)rsdk_align_up(64 + f->len, RSDK_FRAME_ALIGN);
    if (64ull + f->len > chunk_bytes) return RSDK_E_NOSPACE; /* 单帧超过一个chunk */

    /* 放不下 → 闭合当前段, 开新段(新chunk) */
    if (w->frame_count > 0 && w->cur_off + need > chunk_bytes) {
        rsdk_err_t rc = finalize_seg(w); if (rc) return rc;
        rc = start_seg(w); if (rc) return rc;
    }
    /* 组帧 + 写盘: 容忍坏块——同位置重试1次→标坏 chunk→关好前缀→换 chunk 续写本帧 */
    uint64_t bad_cap = rsdk_dev_sb(w->d)->chunk_count;   /* 全坏兜底上限 */
    for (uint64_t bad_try = 0; ; bad_try++) {
        if (w->frame_count == 0) {                       /* 段首: start_time + OPEN 槽 */
            w->start_time = (uint32_t)f->wall_time;
            rsdk_index_slot_t s; fill_slot(w, &s, 0);
            rsdk_index_write(w->d, &s);
            w->open_written = 1;
        }
        rsdk_frame_hdr_t h; memset(&h, 0, sizeof h);
        memcpy(h.magic, RSDK_FRAME_MAGIC, 8);
        h.chn = f->chn; h.stream = f->stream; h.codec = f->codec; h.frame_type = f->frame_type;
        h.rec_kind = RSDK_RK_FRAME;                 /* 自描述:帧 */
        h.rectype  = (uint8_t)w->rectype;           /* 所属段类型(0 连续/事件类型) */
        h.event_id = w->cur_event_id;               /* 事件标签(连续轨命中/事件段;0=无) */
        h.payload_len = f->len; h.seg_id = w->seg_id; h.frame_seq = w->frame_seq;
        h.pts = f->pts; h.wall_time = f->wall_time;
        for (int i = 0; i < 4; i++) h.iv_nonce[i] = (uint8_t)(w->frame_seq >> (i * 8));
        h.payload_crc32 = rsdk_crc32(f->data, f->len);   /* v2: 明文载荷 CRC(掉电/坏块撕裂可检出) */

        struct rsdk_crypto *cr = rsdk_dev_crypto(w->d);
        if (f->len > w->pcap) { w->pbuf = realloc(w->pbuf, f->len); w->pcap = f->len; if (!w->pbuf) return RSDK_E_IO; }
        memcpy(w->pbuf, f->data, f->len);
        if (cr) { h.enc = 1; rsdk_crypto_xcrypt(cr, w->seg_id, w->frame_seq, 0, w->pbuf, f->len); }
        else    { h.enc = 0; }
        h.hdr_crc32 = 0; h.hdr_crc32 = rsdk_crc32(&h, sizeof h);

        uint64_t off = w->base_off + w->cur_off;
        rsdk_err_t wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off, &h, sizeof h);
        if (!wr) wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off + 64, w->pbuf, f->len);
        if (!wr) break;                                  /* 写成功 */

        wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off, &h, sizeof h);   /* 重试1次 */
        if (!wr) wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off + 64, w->pbuf, f->len);
        if (!wr) break;

        /* 仍坏 → 标坏 chunk, 关闭本 chunk 上已写好前缀, 换 chunk 重写本帧 */
        rsdk_dev_mark_bad_chunk(w->d, w->chunk);
        if (w->frame_count == 0)
            rsdk_index_invalidate_chunk(w->d, w->chunk); /* 无好帧: 清除悬空 OPEN 槽 */
        finalize_seg(w);                                 /* frame_count>0→关好前缀; 0→no-op */
        if (bad_try >= bad_cap) return RSDK_E_IO;        /* 全坏兜底 */
        rsdk_err_t src = start_seg(w);                   /* 新 chunk(alloc 跳坏) */
        if (src) return src;                             /* 盘满 */
        /* 回到循环顶: frame_count 已在 start_seg 归零 → 写新段 OPEN 槽 + 头 */
    }

    w->end_off = w->cur_off;
    if (f->frame_type == RSDK_FRAME_I)     /* IDR:入关键帧表(chunk内偏移=本帧起点) */
        kf_record(w, (uint32_t)f->wall_time, w->cur_off);
    w->cur_off += need;
    w->last_time = (uint32_t)f->wall_time;
    w->frame_seq++; w->frame_count++; w->total_bytes += f->len;
    return RSDK_OK;
}

/* 写一条内联标记记录(64B 头 rec_kind≠FRAME + 定长 payload,明文)。用当前段所在 chunk。
 * chunk 放不下 → 闭段+开新段。标记很小很少,不做坏块重试(坏则返回错误由上层容忍)。 */
static rsdk_err_t write_marker(rsdk_writer_t *w, uint8_t kind, uint8_t rectype,
                               uint64_t event_id, uint32_t wall_time,
                               const void *pl, uint32_t pl_len) {
    if (!w || !w->d) return RSDK_E_PARAM;
    uint64_t chunk_bytes = rsdk_dev_sb(w->d)->chunk_sectors * RSDK_SEC;
    uint32_t need = (uint32_t)rsdk_align_up(64 + pl_len, RSDK_FRAME_ALIGN);
    if (64ull + pl_len > chunk_bytes) return RSDK_E_NOSPACE;
    if (w->frame_count > 0 && w->cur_off + need > chunk_bytes) {
        rsdk_err_t rc = finalize_seg(w); if (rc) return rc;
        rc = start_seg(w); if (rc) return rc;
    }
    rsdk_frame_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.magic, RSDK_FRAME_MAGIC, 8);
    h.chn = (uint16_t)w->chn; h.rec_kind = kind; h.rectype = rectype;
    h.event_id = event_id; h.payload_len = pl_len; h.seg_id = w->seg_id;
    h.frame_seq = w->frame_seq; h.wall_time = wall_time; h.enc = 0;
    h.hdr_crc32 = 0; h.hdr_crc32 = rsdk_crc32(&h, sizeof h);
    uint64_t off = w->base_off + w->cur_off;
    rsdk_err_t wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off, &h, sizeof h);
    if (!wr && pl_len) wr = rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off + 64, pl, pl_len);
    if (wr) return wr;
    w->cur_off += need; w->frame_seq++;
    return RSDK_OK;
}

/* 用当前写入器位置 upsert 一条事件槽(音视频定位 = 当前 chunk/off)。open=1 置未闭合。 */
static void evt_slot_upsert(rsdk_writer_t *w, uint64_t event_id, uint8_t rectype,
                            uint32_t start, uint32_t end, uint32_t type_mask, int open) {
    rsdk_evt_slot_t e;
    if (rsdk_evtidx_get(w->d, event_id, &e) != RSDK_OK) {   /* 新事件:建槽,定位音视频 */
        memset(&e, 0, sizeof e);
        e.event_id = event_id; e.chn = (uint16_t)w->chn;
        e.state = RSDK_CLOUD_NONE;
        e.av_disk = rsdk_dev_sb(w->d)->group_disk_index;
        e.av_chunk = w->chunk; e.av_off = w->cur_off;
    }
    e.rectype = rectype; e.type_mask |= type_mask;
    if (start) e.start_time = start;
    e.end_time = end ? end : 0xFFFFFFFFu;
    e.av_end_off = w->cur_off;
    e.flags = RSDK_EVT_VALID | (e.flags & RSDK_EVT_HAS_SNAP) | (open ? RSDK_EVT_OPEN : 0);
    rsdk_evtidx_write(w->d, &e);   /* metadata=off 时 E_NOSPACE, 无害 */
}

/* 事件标记:数据区自描述标记(扫描重建用)+ 事件索引槽(运行期查询用)。一处调用两处落盘。
 * end=0 视为进行中(事件窗未闭合)。 */
rsdk_err_t rsdk_rec_mark_event(rsdk_writer_t *w, uint64_t event_id, uint8_t rectype,
                               uint32_t start, uint32_t end, uint32_t type_mask, uint32_t ref_seg) {
    if (!w) return RSDK_E_PARAM;
    evt_slot_upsert(w, event_id, rectype, start, end, type_mask, end == 0);
    rsdk_mk_event_t e = { .event_start = start, .event_end = end,
                          .type_mask = type_mask, .ref_seg_id = ref_seg };
    return write_marker(w, RSDK_RK_EVENT, rectype, event_id, start, &e, sizeof e);
}

/* 事件窗结束:闭合事件槽(end_time + 清 OPEN)。无对应槽则忽略。 */
rsdk_err_t rsdk_rec_end_event(rsdk_writer_t *w, uint64_t event_id, uint32_t end_time) {
    if (!w) return RSDK_E_PARAM;
    rsdk_evt_slot_t e;
    if (rsdk_evtidx_get(w->d, event_id, &e) != RSDK_OK) return RSDK_E_NOTFOUND;
    e.end_time = end_time ? end_time : e.end_time;
    e.flags &= (uint16_t)~RSDK_EVT_OPEN;
    return rsdk_evtidx_write(w->d, &e);
}

/* 云存态标记:数据区自描述标记(重建用)+ 事件索引槽 state(运行期查询用)。 */
rsdk_err_t rsdk_rec_mark_cloud(rsdk_writer_t *w, uint64_t event_id, uint8_t state, uint32_t ts) {
    if (!w) return RSDK_E_PARAM;
    rsdk_evtidx_patch_state(w->d, event_id, state, 0, ts);   /* 无槽/无区时无害 */
    rsdk_mk_cloud_t c = { .state = state, ._r = {0,0,0} };
    return write_marker(w, RSDK_RK_CLOUD_STATE, 0, event_id, ts, &c, sizeof c);
}

rsdk_err_t rsdk_rec_change_type(rsdk_writer_t *w, int rectype) {
    if (!w) return RSDK_E_PARAM;
    rsdk_err_t rc = finalize_seg(w); if (rc) return rc;
    w->rectype = rectype;
    return start_seg(w);
}

rsdk_err_t rsdk_rec_rotate(rsdk_writer_t *w) {
    if (!w) return RSDK_E_PARAM;
    if (w->frame_count == 0) return RSDK_OK;     /* 空段无需切(避免切出 0 帧段) */
    rsdk_err_t rc = finalize_seg(w); if (rc) return rc;
    return start_seg(w);                          /* 同 rectype 开新段; 上层保证下一帧是 IDR */
}

uint32_t rsdk_rec_frame_count(rsdk_writer_t *w) { return w ? w->frame_count : 0; }

rsdk_err_t rsdk_rec_datasync(rsdk_writer_t *w) {
    if (!w || !w->d) return RSDK_E_PARAM;
    return rsdk_rawdev_sync(rsdk_dev_raw(w->d));  /* 只 fsync 裸设备, 不动 SB/SysTab */
}

rsdk_err_t rsdk_rec_close(rsdk_writer_t *w) {
    if (!w) return RSDK_E_PARAM;
    rsdk_err_t rc = finalize_seg(w);
    if (w->group) {                       /* 多盘: 刷新组内所有盘的 SB/SysTab */
        for (int i = 0; i < rsdk_group_count(w->group); i++)
            rsdk_dev_flush(rsdk_group_dev(w->group, i));
    } else {
        rsdk_dev_flush(w->d);
    }
    free(w->pbuf); free(w->kf); free(w);
    return rc;
}
