/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像写入(设计 §3/§7).
 * 约定: 一个 RecSegment 落在单个 chunk 内; 帧不够放则闭合当前段、开新段(新chunk),
 *       从而回放只需在一个 chunk 内顺序走帧, 无需 chunk 链。 */
#include "rsdk_rec.h"
#include "rsdk_index.h"
#include "rsdk_crypto.h"
#include "rsdk_util.h"
#include <stdlib.h>
#include <string.h>

struct rsdk_writer {
    rsdk_dev_t *d; rsdk_group_t *group; int chn, rectype;
    uint32_t seg_id;
    uint64_t chunk, base_off; uint32_t cur_off;   /* 当前段所在 chunk + 段内偏移 */
    uint32_t frame_seq, frame_count;
    uint64_t total_bytes;
    uint32_t start_time, last_time; uint32_t end_off;
    int      open_written;
    uint8_t *pbuf; size_t pcap;
    rsdk_reclaim_cb reclaim; void *reclaim_user;
};

void rsdk_rec_set_reclaim(rsdk_writer_t *w, rsdk_reclaim_cb cb, void *user) {
    if (w) { w->reclaim = cb; w->reclaim_user = user; }
}

static uint32_t next_seg_id(rsdk_dev_t *d, int chn) {
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint32_t seq = ++st->chn[chn & 31].seg_seq;
    return ((uint32_t)(chn & 0xFF) << 24) | (seq & 0x00FFFFFF);
}

static rsdk_err_t start_seg(rsdk_writer_t *w) {
    if (w->group) {                        /* 多盘: 每段按负载均衡选盘 */
        rsdk_dev_t *picked;
        rsdk_err_t prc = rsdk_balance_pick(w->group, w->chn, &picked);
        if (prc) return prc;
        w->d = picked;
    }
    uint64_t chunk, off;
    rsdk_err_t rc = rsdk_dev_alloc_chunk(w->d, &chunk, &off);
    if (rc) return rc;                          /* stop 策略盘满 → NOSPACE */
    if (rsdk_dev_is_wrapped(w->d)) {            /* 覆盖模式: 回收该 chunk 上旧段索引 + 元数据 */
        rsdk_index_invalidate_chunk(w->d, chunk);
        if (w->reclaim) w->reclaim(w->reclaim_user, rsdk_dev_index(w->d), chunk);
    }
    w->chunk = chunk; w->base_off = off; w->cur_off = 0;
    w->seg_id = next_seg_id(w->d, w->chn);
    w->frame_seq = 0; w->frame_count = 0; w->total_bytes = 0;
    w->start_time = 0; w->last_time = 0; w->end_off = 0; w->open_written = 0;
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
}

static rsdk_err_t finalize_seg(rsdk_writer_t *w) {
    if (w->frame_count == 0) return RSDK_OK;
    rsdk_index_slot_t s; fill_slot(w, &s, 1);
    return rsdk_index_write(w->d, &s);
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
    if (!g || !out) return RSDK_E_PARAM;
    rsdk_writer_t *w = calloc(1, sizeof *w);
    if (!w) return RSDK_E_IO;
    w->group = g; w->chn = chn; w->rectype = rectype;
    rsdk_err_t rc = start_seg(w);
    if (rc) { free(w); return rc; }
    *out = w;
    return RSDK_OK;
}

uint32_t rsdk_rec_seg_id(rsdk_writer_t *w) { return w ? w->seg_id : 0; }
uint64_t rsdk_rec_cur_chunk(rsdk_writer_t *w) { return w ? w->chunk : 0; }

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
    if (w->frame_count == 0) {                 /* 段首: 记录 start_time + 写 OPEN 槽 */
        w->start_time = (uint32_t)f->wall_time;
        rsdk_index_slot_t s; fill_slot(w, &s, 0);
        rsdk_index_write(w->d, &s);
        w->open_written = 1;
    }

    /* 组帧头 */
    rsdk_frame_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.magic, RSDK_FRAME_MAGIC, 8);
    h.chn = f->chn; h.stream = f->stream; h.codec = f->codec; h.frame_type = f->frame_type;
    h.payload_len = f->len; h.seg_id = w->seg_id; h.frame_seq = w->frame_seq;
    h.pts = f->pts; h.wall_time = f->wall_time;
    for (int i = 0; i < 8; i++) h.iv_nonce[i] = (uint8_t)(w->frame_seq >> ((i&3)*8));

    /* 负载: 拷到缓冲, 按特性 AES-CTR 加密 */
    struct rsdk_crypto *cr = rsdk_dev_crypto(w->d);
    if (f->len > w->pcap) { w->pbuf = realloc(w->pbuf, f->len); w->pcap = f->len; if(!w->pbuf) return RSDK_E_IO; }
    memcpy(w->pbuf, f->data, f->len);
    if (cr) { h.enc = 1; rsdk_crypto_xcrypt(cr, w->seg_id, w->frame_seq, 0, w->pbuf, f->len); }
    else    { h.enc = 0; }
    h.hdr_crc32 = 0; h.hdr_crc32 = rsdk_crc32(&h, sizeof h);

    /* 写盘: 帧头(64) + 负载 */
    uint64_t off = w->base_off + w->cur_off;
    rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off, &h, sizeof h);
    rsdk_rawdev_pwrite(rsdk_dev_raw(w->d), off + 64, w->pbuf, f->len);

    w->end_off = w->cur_off;
    w->cur_off += need;
    w->last_time = (uint32_t)f->wall_time;
    w->frame_seq++; w->frame_count++; w->total_bytes += f->len;
    return RSDK_OK;
}

rsdk_err_t rsdk_rec_change_type(rsdk_writer_t *w, int rectype) {
    if (!w) return RSDK_E_PARAM;
    rsdk_err_t rc = finalize_seg(w); if (rc) return rc;
    w->rectype = rectype;
    return start_seg(w);
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
    free(w->pbuf); free(w);
    return rc;
}
