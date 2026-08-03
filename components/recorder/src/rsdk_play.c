/* Copyright (C) 2025-2026, Nightowl DG. RSDK 检索/回放(设计 §6.2/§7). */
#include "rsdk_play.h"
#include "rsdk_crypto.h"
#include "rsdk_util.h"
#include <stdlib.h>
#include <string.h>

struct rsdk_player {
    rsdk_dev_t *d; rsdk_index_slot_t seg;
    uint64_t base_off, chunk_bytes; uint32_t cur_off, end_off;
    uint8_t *buf; size_t cap;
};

rsdk_err_t rsdk_play_open(rsdk_dev_t *d, const rsdk_index_slot_t *seg, rsdk_player_t **out) {
    if (!d || !seg || !out) return RSDK_E_PARAM;
    rsdk_player_t *p = calloc(1, sizeof *p);
    if (!p) return RSDK_E_IO;
    p->d = d; p->seg = *seg;
    p->chunk_bytes = rsdk_dev_sb(d)->chunk_sectors * RSDK_SEC;
    p->base_off = rsdk_dev_sb(d)->data_start_sec * RSDK_SEC + seg->start_chunk * p->chunk_bytes;
    p->cur_off = seg->start_off; p->end_off = seg->end_off;
    *out = p;
    return RSDK_OK;
}

static rsdk_err_t read_hdr(rsdk_player_t *p, uint32_t off, rsdk_frame_hdr_t *h) {
    rsdk_rawdev_pread(rsdk_dev_raw(p->d), p->base_off + off, h, sizeof *h);
    if (memcmp(h->magic, RSDK_FRAME_MAGIC, 8) != 0) return RSDK_E_NOTFOUND;
    rsdk_frame_hdr_t t = *h; t.hdr_crc32 = 0;
    if (rsdk_crc32(&t, sizeof t) != h->hdr_crc32) return RSDK_E_CORRUPT;
    return RSDK_OK;
}

rsdk_err_t rsdk_play_seek_pts(rsdk_player_t *p, uint64_t pts) {
    if (!p) return RSDK_E_PARAM;
    uint32_t off = p->seg.start_off, best = p->seg.start_off;
    while (off <= p->end_off) {
        rsdk_frame_hdr_t h;
        if (read_hdr(p, off, &h) != RSDK_OK) break;
        if (h.pts <= pts) best = off; else break;
        off = (uint32_t)rsdk_align_up(off + 64 + h.payload_len, RSDK_FRAME_ALIGN);
    }
    p->cur_off = best;
    return RSDK_OK;
}

rsdk_err_t rsdk_play_next_frame(rsdk_player_t *p, rsdk_frame_hdr_t *hdr,
                                const uint8_t **data, uint32_t *len) {
    if (!p || !hdr || !data || !len) return RSDK_E_PARAM;
    if (p->cur_off > p->end_off) return RSDK_E_NOTFOUND;
    rsdk_frame_hdr_t h;
    rsdk_err_t rc = read_hdr(p, p->cur_off, &h);
    if (rc) return rc;
    if (h.payload_len > p->cap) { p->buf = realloc(p->buf, h.payload_len); p->cap = h.payload_len; if(!p->buf) return RSDK_E_IO; }
    rsdk_rawdev_pread(rsdk_dev_raw(p->d), p->base_off + p->cur_off + 64, p->buf, h.payload_len);
    if (h.enc) {
        struct rsdk_crypto *cr = rsdk_dev_crypto(p->d);
        if (!cr) return RSDK_E_CRYPTO;              /* 加密帧但无密钥(需加密固件/正确SN) */
        rsdk_crypto_xcrypt(cr, h.seg_id, h.frame_seq, 0, p->buf, h.payload_len);
    }
    *hdr = h; *data = p->buf; *len = h.payload_len;
    p->cur_off = (uint32_t)rsdk_align_up(p->cur_off + 64 + h.payload_len, RSDK_FRAME_ALIGN);
    return RSDK_OK;
}

void rsdk_play_close(rsdk_player_t *p) { if (p) { free(p->buf); free(p); } }
