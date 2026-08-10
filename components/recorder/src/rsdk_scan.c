/* Copyright (C) 2025-2026, Nightowl DG. RSDK 扫描重建实现。见 rsdk_scan.h。 */
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

#define SCAN_MAX_SEG 8192
#define SCAN_MAX_EVT 8192

typedef struct {
    uint32_t seg_id; uint16_t chn; uint8_t rectype; uint8_t stream; uint8_t has_event;
    uint32_t t0, t1; uint32_t fcount; uint64_t bytes;
    uint64_t start_chunk; uint32_t start_off; uint64_t end_chunk; uint32_t end_off;
} seg_acc_t;
typedef struct {
    uint64_t event_id; uint16_t chn; uint8_t rectype;
    uint32_t t0, t1; uint32_t type_mask;
} evt_acc_t;

static seg_acc_t *seg_find(seg_acc_t *a, int *n, uint32_t seg_id) {
    for (int i = 0; i < *n; i++) if (a[i].seg_id == seg_id) return &a[i];
    if (*n >= SCAN_MAX_SEG) return NULL;
    seg_acc_t *s = &a[(*n)++]; memset(s, 0, sizeof *s);
    s->seg_id = seg_id; s->t0 = 0xFFFFFFFFu; return s;
}
static evt_acc_t *evt_find(evt_acc_t *a, int *n, uint64_t event_id) {
    for (int i = 0; i < *n; i++) if (a[i].event_id == event_id) return &a[i];
    if (*n >= SCAN_MAX_EVT) return NULL;
    evt_acc_t *e = &a[(*n)++]; memset(e, 0, sizeof *e);
    e->event_id = event_id; e->t0 = 0xFFFFFFFFu; return e;
}

rsdk_err_t rsdk_scan_rebuild(rsdk_dev_t *d, void *meta, int *out_segs, int *out_events) {
    if (!d) return RSDK_E_PARAM;
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    rsdk_rawdev_t *raw = rsdk_dev_raw(d);
    if (!sb || !raw) return RSDK_E_PARAM;
    uint64_t cs = sb->chunk_sectors, ds = sb->data_start_sec, ncc = sb->chunk_count;
    uint64_t chunk_bytes = cs * RSDK_SEC;
    if (chunk_bytes < 64) return RSDK_E_PARAM;

    seg_acc_t *segs = calloc(SCAN_MAX_SEG, sizeof *segs);
    evt_acc_t *evts = calloc(SCAN_MAX_EVT, sizeof *evts);
    if (!segs || !evts) { free(segs); free(evts); return RSDK_E_IO; }
    int nseg = 0, nevt = 0;
    uint8_t hdr[64];

    for (uint64_t c = 0; c < ncc; c++) {
        uint64_t coff = (ds + c * cs) * RSDK_SEC;
        uint64_t roff = 0;
        while (roff + 64 <= chunk_bytes) {
            if (rsdk_rawdev_pread(raw, coff + roff, hdr, 64) != RSDK_OK) break;
            rsdk_frame_hdr_t *h = (rsdk_frame_hdr_t *)hdr;
            if (memcmp(h->magic, RSDK_FRAME_MAGIC, 8) != 0) break;   /* 记录尽头(零/垃圾) */
            uint32_t crc = h->hdr_crc32; h->hdr_crc32 = 0;
            if (rsdk_crc32(hdr, 64) != crc) break;                    /* 头损坏 → 停本 chunk */
            uint32_t plen = h->payload_len;
            uint32_t adv = (uint32_t)rsdk_align_up(64ull + plen, RSDK_FRAME_ALIGN);
            if (adv == 0 || roff + adv > chunk_bytes + RSDK_FRAME_ALIGN) break;

            if (h->rec_kind == RSDK_RK_FRAME) {
                seg_acc_t *s = seg_find(segs, &nseg, h->seg_id);
                if (s) {
                    s->chn = h->chn; s->rectype = h->rectype; s->stream = h->stream;
                    uint32_t wt = (uint32_t)h->wall_time;
                    if (wt < s->t0) { s->t0 = wt; s->start_chunk = c; s->start_off = (uint32_t)roff; }
                    if (wt >= s->t1) { s->t1 = wt; s->end_chunk = c; s->end_off = (uint32_t)roff; }
                    s->fcount++; s->bytes += plen;
                    if (h->event_id) {
                        s->has_event = 1;
                        evt_acc_t *e = evt_find(evts, &nevt, h->event_id);
                        if (e) { e->chn = h->chn; if (!e->rectype) e->rectype = h->rectype;
                                 if (wt < e->t0) e->t0 = wt; if (wt > e->t1) e->t1 = wt;
                                 if (h->rectype > 0 && h->rectype < 32) e->type_mask |= (1u << h->rectype); }
                    }
                }
            } else if (h->rec_kind == RSDK_RK_EVENT) {
                rsdk_mk_event_t mk; memset(&mk, 0, sizeof mk);
                if (plen >= sizeof mk) rsdk_rawdev_pread(raw, coff + roff + 64, &mk, sizeof mk);
                evt_acc_t *e = evt_find(evts, &nevt, h->event_id);
                if (e) { e->chn = h->chn; e->rectype = h->rectype;
                         if (mk.event_start && mk.event_start < e->t0) e->t0 = mk.event_start;
                         if (mk.event_end   && mk.event_end   > e->t1) e->t1 = mk.event_end;
                         e->type_mask |= mk.type_mask; }
            }
            /* CLOUD_STATE: 云存终态由上层(cloud)重放 meta;此处不改索引。 */
            roff += adv;
        }
    }

    /* 重写索引槽 */
    for (int i = 0; i < nseg; i++) {
        seg_acc_t *a = &segs[i];
        rsdk_index_slot_t s; memset(&s, 0, sizeof s);
        s.seg_id = a->seg_id; s.chn = a->chn; s.rectype = a->rectype; s.stream = a->stream;
        s.flags = RSDK_SLOT_VALID | ((a->has_event || a->rectype != RSDK_REC_CONTINUOUS) ? RSDK_SLOT_EVENT : 0);
        s.start_time = (a->t0 == 0xFFFFFFFFu) ? 0 : a->t0;
        s.end_time = a->t1;
        s.frame_count = a->fcount; s.total_bytes = a->bytes;
        s.start_disk = sb->group_disk_index; s.end_disk = s.start_disk;
        s.start_chunk = a->start_chunk; s.start_off = a->start_off;
        s.end_chunk = a->end_chunk; s.end_off = a->end_off;
        rsdk_index_write(d, &s);
    }

#if RSDK_CFG_METADATA
    /* 回填事件骨架(AI_EVENT):rectype/type_mask/duration 数值,上层按需翻译成 eventTypes 名。
     * 富 AI 细节不重建。 */
    if (meta) {
        for (int i = 0; i < nevt; i++) {
            evt_acc_t *e = &evts[i];
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
    if (out_events) *out_events = nevt;
    free(segs); free(evts);
    return RSDK_OK;
}
