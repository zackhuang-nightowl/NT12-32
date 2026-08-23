/* Copyright (C) 2025-2026, Nightowl DG. RSDK 扫描重建实现。见 rsdk_scan.h。
 *
 * 按 chunk 逐块重建(★段=chunk:chunk 写满即封口开新段,见 rsdk_rec.c)。每块顺读帧头(64B,跳过载荷)
 * 聚合出该段的 seg_id/chn/stream/rectype/start/end/帧数/字节,写一条索引槽。内存 O(1),可扩到百万段——
 * 不再有旧实现 SCAN_MAX_SEG=8192 的段上限与 seg_find 的 O(n²)。
 * 事件跨段/跨块聚合(一个事件可覆盖多段),用可增长数组累计,末尾回填 meta 事件骨架(AI_EVENT)。 */
#include "rsdk_scan.h"
#include "rsdk_rawdev.h"
#include "rsdk_index.h"
#include "rsdk_evtidx.h"
#include "rsdk_feature.h"    /* RSDK_CFG_METADATA(否则整个元数据/事件槽块被编译掉) */
#include "rsdk_util.h"
#if RSDK_CFG_METADATA
#include "rsdk_pic.h"     /* rsdk_pic_hdr_t: 顺扫 MetaRegion 回填截图指针 */
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

/* 事件聚合(跨段/跨块;可增长,无固定上限)。av_* = 音视频定位;state = 云存态(重建源 RK_CLOUD_STATE)。 */
typedef struct {
    uint64_t event_id; uint16_t chn; uint8_t rectype; uint8_t state;
    uint32_t t0, t1, type_mask;
    uint16_t av_disk; uint64_t av_chunk; uint32_t av_off, av_end_off; int have_av;
} evt_acc_t;
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
                             if (!e->have_av || wt < e->t0) {   /* 最早帧 → 音视频定位 */
                                 e->av_disk = rsdk_dev_index(d); e->av_chunk = c;
                                 e->av_off = (uint32_t)roff; e->have_av = 1;
                             }
                             if (wt < e->t0) e->t0 = wt; if (wt > e->t1) e->t1 = wt;
                             e->av_end_off = (uint32_t)roff;
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
        } else if (h->rec_kind == RSDK_RK_CLOUD_STATE && evm) {
            rsdk_mk_cloud_t mk; memset(&mk, 0, sizeof mk);
            if (plen >= sizeof mk) rsdk_rawdev_pread(raw, coff + roff + 64, &mk, sizeof mk);
            evt_acc_t *e = evt_touch(evm, h->event_id);
            if (e) { uint8_t stt = mk.state;
                     if (stt == RSDK_CLOUD_UPLOADING) stt = RSDK_CLOUD_RETRY;  /* 中断上传→待重试补传 */
                     e->state = stt; }
        }
        /* RK_KEYIDX / SEG_OPEN / SEG_CLOSE 标记:不影响段时间范围,跳过(adv 已略过其载荷)。 */
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

#if RSDK_CFG_METADATA
/* 顺扫 MetaRegion 的 PIC0 头(rsdk_pic 已按 512 对齐分配 → 只查扇区边界),
 * 按 event_id 回填事件槽截图指针。返回回填条数。 */
static int scan_metaregion_pics(rsdk_dev_t *d)
{
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (!sb || !st || st->meta_bytes == 0) return 0;
    rsdk_rawdev_t *raw = rsdk_dev_raw(d);
    uint64_t chunk_bytes = sb->chunk_sectors * RSDK_SEC;
    uint64_t meta_base = sb->data_start_sec * RSDK_SEC + sb->meta_start_chunk * chunk_bytes;
    uint64_t meta_bytes = st->meta_bytes;
    int patched = 0;
    rsdk_pic_hdr_t h;
    for (uint64_t off = 0; off + sizeof h <= meta_bytes; off += RSDK_SEC) {
        if (rsdk_rawdev_pread(raw, meta_base + off, &h, sizeof h) != RSDK_OK) break;
        if (memcmp(h.magic, "PIC0", 4) != 0) continue;
        rsdk_pic_hdr_t t = h; t.crc32 = 0;
        if (rsdk_crc32(&t, sizeof t) != h.crc32) continue;    /* 非 PIC0 头 / 损坏 */
        uint64_t total = rsdk_align_up(sizeof(rsdk_pic_hdr_t) + h.jpeg_len, RSDK_SEC);
        if (rsdk_evtidx_patch_snap(d, h.event_id, sb->group_disk_index,
                                   meta_base + off, (uint32_t)total, h.ts) == RSDK_OK)
            patched++;
    }
    return patched;
}
#endif

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
    /* 收集事件的条件:盘上有事件索引区(需重建事件槽)或调用方要 meta.db 骨架。
     * 事件槽重建不依赖 meta.db(掉库也要恢复事件全链)。 */
    int want_events = (rsdk_dev_systab(d)->evtidx_slot_count > 0) || (meta != NULL);

    for (uint64_t c = 0; c < ncc; c++) {
        if (abort && *abort) break;
        if (rsdk_dev_is_bad_chunk(d, c)) continue;         /* 坏 chunk 跳过 */
        int fc = parse_chunk(d, c, 0, &seg, want_events ? &evm : NULL);
        if (fc > 0) { write_slot_for(d, c, &seg); nseg++; }
        if (progress && (c & 0x3FF) == 0) progress(user, c, ncc);
    }
    if (progress) progress(user, ncc, ncc);

#if RSDK_CFG_METADATA
    if (want_events) {
        /* 事件索引槽(盘上权威:音视频定位 + 云存态)。无对应视频帧的纯标记事件跳过。
         * meta.db 是可丢的富 AI 元数据边车,rebuild 不重建它(数据已随 meta.db 一起丢,不造假)。 */
        for (int i = 0; i < evm.n; i++) {
            if (abort && *abort) break;
            evt_acc_t *e = &evm.a[i];
            if (!e->have_av) continue;
            rsdk_evt_slot_t es; memset(&es, 0, sizeof es);
            es.event_id = e->event_id; es.chn = e->chn; es.rectype = e->rectype;
            es.state = e->state; es.type_mask = e->type_mask;
            es.start_time = (e->t0 == 0xFFFFFFFFu) ? 0 : e->t0; es.end_time = e->t1;
            es.av_disk = e->av_disk; es.av_chunk = e->av_chunk;
            es.av_off = e->av_off; es.av_end_off = e->av_end_off;
            es.flags = RSDK_EVT_VALID;
            rsdk_evtidx_write(d, &es);
        }
        /* 顺扫 MetaRegion PIC0 → 回填事件槽截图指针(全链恢复的最后一步)。 */
        scan_metaregion_pics(d);
    }
    (void)meta;   /* rebuild 不写 meta.db(元数据可丢) */
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
