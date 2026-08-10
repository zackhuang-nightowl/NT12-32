/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像索引(设计 §6, 冻结 §3). */
#include "rsdk_index.h"
#include "rsdk_util.h"
#include <string.h>

#define SLOT_SZ ((uint64_t)sizeof(rsdk_index_slot_t))

static uint64_t slot_off(rsdk_dev_t *d, uint32_t i) {
    return rsdk_dev_systab(d)->index_start_sec * RSDK_SEC + (uint64_t)i * SLOT_SZ;
}
static void rd_slot(rsdk_dev_t *d, uint32_t i, rsdk_index_slot_t *s) {
    rsdk_rawdev_pread(rsdk_dev_raw(d), slot_off(d, i), s, SLOT_SZ);
}
static void wr_slot(rsdk_dev_t *d, uint32_t i, const rsdk_index_slot_t *s) {
    rsdk_rawdev_pwrite(rsdk_dev_raw(d), slot_off(d, i), s, SLOT_SZ);
}

rsdk_err_t rsdk_index_write(rsdk_dev_t *d, const rsdk_index_slot_t *in)
{
    if (!d || !in) return RSDK_E_PARAM;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    rsdk_index_slot_t s = *in;
    s.crc32 = 0; s.crc32 = rsdk_crc32(&s, SLOT_SZ);

    /* upsert: 在最近写过的槽里回找同 seg_id(闭合 open 段常用); 向后回溯有界 */
    uint32_t n = st->index_slot_count, scan = n < 8192 ? n : 8192;
    for (uint32_t k = 1; k <= scan; k++) {
        uint32_t i = (st->index_next + n - k) % n;
        rsdk_index_slot_t cur; rd_slot(d, i, &cur);
        if ((cur.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && cur.seg_id == in->seg_id) {
            wr_slot(d, i, &s); rsdk_rawdev_sync(rsdk_dev_raw(d));
            return RSDK_OK;
        }
        if (cur.flags == 0 && cur.seg_id == 0) break; /* 到达未写区 */
    }
    /* append at index_next(环形) */
    wr_slot(d, st->index_next, &s);
    st->index_next = (st->index_next + 1) % n;
    rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_flush(d);
    return RSDK_OK;
}

int rsdk_index_query(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                     int rectype, rsdk_index_slot_t *out, int cap)
{
    if (!d || !out || cap <= 0) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (t1 == 0) t1 = 0xFFFFFFFFu;
    int found = 0;
    for (uint32_t i = 0; i < st->index_slot_count && found < cap; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        if (!(s.flags & RSDK_SLOT_VALID)) continue;
        rsdk_index_slot_t t = s; t.crc32 = 0;
        if (rsdk_crc32(&t, SLOT_SZ) != s.crc32) continue;      /* 跳损坏槽 */
        uint32_t e = (s.end_time == 0xFFFFFFFFu) ? t1 : s.end_time;
        if (e < t0 || s.start_time > t1) continue;             /* 时间不相交 */
        if (chn >= 0 && s.chn != chn) continue;
        if (rectype >= 0 && s.rectype != rectype) continue;
        out[found++] = s;
    }
    /* 简单按 start_time 升序 */
    for (int a = 0; a < found; a++) for (int b = a+1; b < found; b++)
        if (out[b].start_time < out[a].start_time) { rsdk_index_slot_t t=out[a]; out[a]=out[b]; out[b]=t; }
    return found;
}

/* 判断 chunk 是否落在段 [start_chunk, end_chunk] 内, 处理环绕(start > end) */
static int slot_covers_chunk(const rsdk_index_slot_t *s, uint64_t chunk) {
    if (s->start_chunk <= s->end_chunk)
        return chunk >= s->start_chunk && chunk <= s->end_chunk;
    return chunk >= s->start_chunk || chunk <= s->end_chunk;  /* 环绕 */
}

int rsdk_index_invalidate_chunk(rsdk_dev_t *d, uint64_t chunk)
{
    if (!d) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    int cleared = 0;
    for (uint32_t i = 0; i < st->index_slot_count; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        if ((s.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && slot_covers_chunk(&s, chunk)) {
            s.flags = 0;                       /* 作废该段(数据已/将被覆盖) */
            s.crc32 = 0; s.crc32 = rsdk_crc32(&s, SLOT_SZ);
            wr_slot(d, i, &s); cleared++;
        }
    }
    if (cleared) rsdk_rawdev_sync(rsdk_dev_raw(d));
    return cleared;
}

rsdk_err_t rsdk_index_earliest(rsdk_dev_t *d, uint32_t *epoch)
{
    if (!d || !epoch) return RSDK_E_PARAM;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint32_t best = 0xFFFFFFFFu; int any = 0;
    for (uint32_t i = 0; i < st->index_slot_count; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        if ((s.flags & RSDK_SLOT_VALID) && s.start_time < best) { best = s.start_time; any = 1; }
    }
    if (!any) return RSDK_E_NOTFOUND;
    *epoch = best; return RSDK_OK;
}
