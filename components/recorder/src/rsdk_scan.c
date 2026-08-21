/* Copyright (C) 2025-2026, Nightowl DG. RSDK 扫描重建实现。见 rsdk_scan.h。
 *
 * 按 chunk 逐块重建(★段=chunk:chunk 写满即封口开新段,见 rsdk_rec.c)。每块顺读帧头(64B,跳过载荷)
 * 聚合出该段的 seg_id/chn/stream/rectype/start/end/帧数/字节,写一条索引槽。内存 O(1),可扩到百万段——
 * 不再有旧实现 SCAN_MAX_SEG=8192 的段上限与 seg_find 的 O(n²)。
 * 事件跨段/跨块聚合(一个事件可覆盖多段),用可增长数组累计,末尾回填 meta 事件骨架(AI_EVENT)。 */
#include "rsdk_scan.h"
#include "rsdk_rawdev.h"
#include "rsdk_index.h"
#include "rsdk_util.h"
#if RSDK_CFG_METADATA
#include "rsdk_meta.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 单段(一个 chunk)聚合结果 */
typedef struct {
    uint32_t seg_id; uint16_t chn; uint8_t rectype; uint8_t stream;
    uint32_t t0, t1; uint32_t fcount; uint64_t bytes;
    uint32_t start_off, end_off; int has_event;
} chunk_seg_t;

/* 事件聚合(跨段/跨块;可增长,无固定上限) */
typedef struct { uint64_t event_id; uint16_t chn; uint8_t rectype; uint32_t t0, t1, type_mask; } evt_acc_t;
typedef struct { evt_acc_t *a; int n, cap; } evt_map_t;

static evt_acc_t *evt_touch(evt_map_t *m, uint64_t event_id)
{
    if (!m) return NULL;
    for (int i = 0; i < m->n; i++) if (m->a[i].event_id == event_id) return &m->a[i];
    if (m->n == m->cap) {
        int nc = m->cap ? m->cap * 2 : 64;
        evt_acc_t *na = (evt_acc_t *)realloc(m->a, (size_t)nc * sizeof *na);
        if (!na) return NULL;   /* OOM: 丢弃该事件,不影响索引重建 */
        m->a = na; m->cap = nc;
    }
    evt_acc_t *e = &m->a[m->n++]; memset(e, 0, sizeof *e);
    e->event_id = event_id; e->t0 = 0xFFFFFFFFu; return e;
}

/* 顺读 chunk c 的帧头,聚合出该段(want_seg!=0 时仅统计该 seg_id 的帧)。
 * evm!=NULL 时顺带收集事件。返回聚合到的帧数(0=空/无匹配)。 */
static int parse_chunk(rsdk_dev_t *d, uint64_t c, uint32_t want_seg,
                       chunk_seg_t *seg, evt_map_t *evm)
{
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    rsdk_rawdev_t *raw = rsdk_dev_raw(d);
    uint64_t chunk_bytes = sb->chunk_sectors * RSDK_SEC;
    uint64_t coff = (sb->data_start_sec + c * sb->chunk_sectors) * RSDK_SEC;
    uint8_t hdr[64];

    memset(seg, 0, sizeof *seg);
    seg->t0 = 0xFFFFFFFFu;
    int got = 0;
    uint64_t roff = 0;
    while (roff + 64 <= chunk_bytes) {
        if (rsdk_rawdev_pread(raw, coff + roff, hdr, 64) != RSDK_OK) break;
        rsdk_frame_hdr_t *h = (rsdk_frame_hdr_t *)hdr;
        if (memcmp(h->magic, RSDK_FRAME_MAGIC, 8) != 0) break;       /* 记录尽头(零/垃圾) */
        uint32_t crc = h->hdr_crc32; h->hdr_crc32 = 0;
        if (rsdk_crc32(hdr, 64) != crc) break;                        /* 头损坏 → 停本 chunk */
        uint32_t plen = h->payload_len;
        uint32_t adv = (uint32_t)rsdk_align_up(64ull + plen, RSDK_FRAME_ALIGN);
        if (adv == 0 || roff + adv > chunk_bytes + RSDK_FRAME_ALIGN) break;

        if (h->rec_kind == RSDK_RK_FRAME) {
            if (want_seg == 0 || h->seg_id == want_seg) {
                uint32_t wt = (uint32_t)h->wall_time;
                if (!got) { seg->seg_id = h->seg_id; seg->chn = h->chn;
                            seg->rectype = h->rectype; seg->stream = h->stream; }
                if (wt < seg->t0) { seg->t0 = wt; seg->start_off = (uint32_t)roff; }
                if (wt >= seg->t1) { seg->t1 = wt; seg->end_off = (uint32_t)roff; }
                seg->fcount++; seg->bytes += plen; got++;
                if (h->event_id) {
                    seg->has_event = 1;
                    evt_acc_t *e = evt_touch(evm, h->event_id);
                    if (e) { e->chn = h->chn; if (!e->rectype) e->rectype = h->rectype;
                             if (wt < e->t0) e->t0 = wt; if (wt > e->t1) e->t1 = wt;
                             if (h->rectype > 0 && h->rectype < 32) e->type_mask |= (1u << h->rectype); }
                }
            }
        } else if (h->rec_kind == RSDK_RK_EVENT && evm) {
            rsdk_mk_event_t mk; memset(&mk, 0, sizeof mk);
            if (plen >= sizeof mk) rsdk_rawdev_pread(raw, coff + roff + 64, &mk, sizeof mk);
            evt_acc_t *e = evt_touch(evm, h->event_id);
            if (e) { e->chn = h->chn; e->rectype = h->rectype;
                     if (mk.event_start && mk.event_start < e->t0) e->t0 = mk.event_start;
                     if (mk.event_end   && mk.event_end   > e->t1) e->t1 = mk.event_end;
                     e->type_mask |= mk.type_mask; }
        }
        /* CLOUD_STATE / SEG_OPEN / SEG_CLOSE 标记:不影响段时间范围,跳过。 */
        roff += adv;
    }
    return got;
}

static void write_slot_for(rsdk_dev_t *d, uint64_t c, const chunk_seg_t *seg)
{
    rsdk_index_slot_t s; memset(&s, 0, sizeof s);
    s.seg_id = seg->seg_id; s.chn = seg->chn; s.rectype = seg->rectype; s.stream = seg->stream;
    s.flags = RSDK_SLOT_VALID | ((seg->has_event || seg->rectype != RSDK_REC_CONTINUOUS) ? RSDK_SLOT_EVENT : 0);
    s.start_time = (seg->t0 == 0xFFFFFFFFu) ? 0 : seg->t0;
    s.end_time = seg->t1;
    s.frame_count = seg->fcount; s.total_bytes = seg->bytes;
    s.start_disk = rsdk_dev_index(d); s.end_disk = s.start_disk;
    s.start_chunk = c; s.start_off = seg->start_off;
    s.end_chunk = c; s.end_off = seg->end_off;
    rsdk_index_write(d, &s);   /* 锁内 seg_id upsert:与实时录像/覆盖回收串行,幂等 */
}

rsdk_err_t rsdk_scan_rebuild2(rsdk_dev_t *d, void *meta, int *out_segs, int *out_events,
                              rsdk_scan_progress_fn progress, void *user,
                              const volatile int *abort)
{
    if (!d) return RSDK_E_PARAM;
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    rsdk_rawdev_t *raw = rsdk_dev_raw(d);
    if (!sb || !raw) return RSDK_E_PARAM;
    uint64_t ncc = sb->chunk_count;
    uint64_t chunk_bytes = sb->chunk_sectors * RSDK_SEC;
    if (chunk_bytes < 64) return RSDK_E_PARAM;

    evt_map_t evm = { NULL, 0, 0 };
    int nseg = 0;
    chunk_seg_t seg;

    for (uint64_t c = 0; c < ncc; c++) {
        if (abort && *abort) break;
        if (rsdk_dev_is_bad_chunk(d, c)) continue;         /* 坏 chunk 跳过 */
        int fc = parse_chunk(d, c, 0, &seg, meta ? &evm : NULL);
        if (fc > 0) { write_slot_for(d, c, &seg); nseg++; }
        if (progress && (c & 0x3FF) == 0) progress(user, c, ncc);
    }
    if (progress) progress(user, ncc, ncc);

#if RSDK_CFG_METADATA
    if (meta) {
        for (int i = 0; i < evm.n; i++) {
            if (abort && *abort) break;
            evt_acc_t *e = &evm.a[i];
            uint32_t t0 = (e->t0 == 0xFFFFFFFFu) ? 0 : e->t0;
            uint32_t dur = (e->t1 > t0) ? (e->t1 - t0) : 0;
            char json[128];
            int jl = snprintf(json, sizeof json,
                "{\"rectype\":%u,\"type_mask\":%u,\"duration\":%u,\"rebuilt\":1}",
                e->rectype, e->type_mask, dur);
            rsdk_meta_key_t k; memset(&k, 0, sizeof k);
            k.ts = t0; k.chn = (int16_t)e->chn; k.event_id = e->event_id; k.doc_type = RSDK_DOC_AI_EVENT;
            rsdk_meta_put(meta, &k, json, (size_t)jl, NULL);
        }
    }
#else
    (void)meta;
#endif

    if (out_segs) *out_segs = nseg;
    if (out_events) *out_events = evm.n;
    free(evm.a);
    return RSDK_OK;
}

rsdk_err_t rsdk_scan_rebuild(rsdk_dev_t *d, void *meta, int *out_segs, int *out_events)
{
    return rsdk_scan_rebuild2(d, meta, out_segs, out_events, NULL, NULL, NULL);
}

rsdk_err_t rsdk_scan_finalize_open(rsdk_dev_t *d, int *out_fixed)
{
    if (out_fixed) *out_fixed = 0;
    if (!d) return RSDK_E_PARAM;
    rsdk_index_slot_t open[64];
    int n = rsdk_index_list_open(d, open, 64);
    int fixed = 0;
    for (int i = 0; i < n; i++) {
        chunk_seg_t seg;
        int fc = parse_chunk(d, open[i].start_chunk, open[i].seg_id, &seg, NULL);
        if (fc <= 0) continue;                     /* 该 chunk 无该段好帧 → 留原样(查询仍按 t1 当"到现在") */
        rsdk_index_slot_t s = open[i];
        s.flags = RSDK_SLOT_VALID;                 /* OPEN → 封口 */
        s.end_time = seg.t1;
        s.end_chunk = open[i].start_chunk; s.end_off = seg.end_off;
        s.frame_count = seg.fcount; s.total_bytes = seg.bytes;
        rsdk_index_write(d, &s);                   /* seg_id upsert 覆盖原 OPEN 槽 */
        fixed++;
    }
    if (out_fixed) *out_fixed = fixed;
    return RSDK_OK;
}
