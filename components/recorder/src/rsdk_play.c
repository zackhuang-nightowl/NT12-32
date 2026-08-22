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

/* 定位并读出本段关键帧表(段闭合时写在末帧之后的 RK_KEYIDX 记录)。
 * 成功:*out=malloc 的条目数组(调用方 free),*n=条目数,返回 0;无表返回 <0。 */
static int load_keyframes(rsdk_player_t *p, rsdk_kf_entry_t **out, uint32_t *n) {
    rsdk_frame_hdr_t h;
    if (read_hdr(p, p->end_off, &h) != RSDK_OK) return -1;      /* 末帧头 */
    uint32_t kf_off = (uint32_t)rsdk_align_up(p->end_off + 64 + h.payload_len, RSDK_FRAME_ALIGN);
    if ((uint64_t)kf_off + 64 > p->chunk_bytes) return -1;
    rsdk_frame_hdr_t kh;
    if (read_hdr(p, kf_off, &kh) != RSDK_OK) return -1;
    if (kh.rec_kind != RSDK_RK_KEYIDX || kh.payload_len == 0) return -1;
    uint32_t cnt = kh.payload_len / (uint32_t)sizeof(rsdk_kf_entry_t);
    rsdk_kf_entry_t *arr = malloc((size_t)cnt * sizeof *arr);
    if (!arr) return -1;
    rsdk_rawdev_pread(rsdk_dev_raw(p->d), p->base_off + kf_off + 64, arr, (size_t)cnt * sizeof *arr);
    *out = arr; *n = cnt;
    return 0;
}

rsdk_err_t rsdk_play_seek(rsdk_player_t *p, uint32_t wall) {
    if (!p) return RSDK_E_PARAM;
    rsdk_kf_entry_t *kf = NULL; uint32_t n = 0;
    if (load_keyframes(p, &kf, &n) == 0 && n > 0) {
        uint32_t best = kf[0].chunk_off;      /* 全部晚于目标则取首个 IDR */
        for (uint32_t i = 0; i < n; i++) {
            if (kf[i].wall_time <= wall) best = kf[i].chunk_off; else break;
        }
        p->cur_off = best; free(kf);
        return RSDK_OK;
    }
    free(kf);
    /* 无表:顺扫,取最后一个 wall_time<=目标 的 IDR */
    uint32_t off = p->seg.start_off, best = p->seg.start_off;
    while (off <= p->end_off) {
        rsdk_frame_hdr_t h;
        if (read_hdr(p, off, &h) != RSDK_OK) break;
        if (h.rec_kind == RSDK_RK_FRAME && h.frame_type == RSDK_FRAME_I) {
            if ((uint32_t)h.wall_time <= wall) best = off; else break;
        }
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
    /* v2: 校验明文载荷 CRC(掉电/坏块撕裂 → 头 CRC 过但载荷坏 → 此处拦截, 不把坏帧喂解码器)。
     * v1 盘该字段为旧 nonce 尾字节, 不校验。注:内联标记(RK_EVENT/CLOUD)payload_crc32=0,
     * 仅对 RK_FRAME 校验;标记照常返回(collect_from_disk/扫描依赖看到标记)。 */
    if (rsdk_dev_version(p->d) >= 2 && h.rec_kind == RSDK_RK_FRAME &&
        rsdk_crc32(p->buf, h.payload_len) != h.payload_crc32) {
        p->cur_off = (uint32_t)rsdk_align_up(p->cur_off + 64 + h.payload_len, RSDK_FRAME_ALIGN);
        return RSDK_E_CORRUPT;                      /* 跳过坏载荷帧 */
    }
    *hdr = h; *data = p->buf; *len = h.payload_len;
    p->cur_off = (uint32_t)rsdk_align_up(p->cur_off + 64 + h.payload_len, RSDK_FRAME_ALIGN);
    return RSDK_OK;
}

void rsdk_play_close(rsdk_player_t *p) { if (p) { free(p->buf); free(p); } }
