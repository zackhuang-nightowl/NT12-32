/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像索引(设计 §6, 冻结 §3). */
#include "rsdk_index.h"
#include "rsdk_util.h"
#include <stdlib.h>
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

/* 开盘顺序扫一次索引, 建 chunk→slot 加速表(seg⊆chunk, 用 start_chunk 作键)。 */
void rsdk_index_load_map(rsdk_dev_t *d)
{
    if (!d) return;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint32_t n = st->index_slot_count;
    enum { BATCH = 256 };
    rsdk_index_slot_t *buf = (rsdk_index_slot_t *)malloc(BATCH * SLOT_SZ);
    if (!buf) return;                                  /* 无缓冲 → 不建表, 回退全扫描 */
    if (rsdk_dev_map_alloc(d) != 0) { free(buf); return; }  /* 建表失败 → 回退 */
    rsdk_dev_lock(d);
    for (uint32_t base = 0; base < n; base += BATCH) {
        uint32_t cnt = (n - base < BATCH) ? (n - base) : BATCH;
        rsdk_rawdev_pread(rsdk_dev_raw(d), slot_off(d, base), buf, cnt * SLOT_SZ);
        for (uint32_t k = 0; k < cnt; k++) {
            rsdk_index_slot_t *s = &buf[k];
            if (!(s->flags & (RSDK_SLOT_VALID | RSDK_SLOT_OPEN))) continue;
            rsdk_index_slot_t t = *s; t.crc32 = 0;
            if (rsdk_crc32(&t, SLOT_SZ) != s->crc32) continue;   /* 跳损坏槽 */
            rsdk_dev_map_set(d, s->start_chunk, base + k);
        }
    }
    rsdk_dev_unlock(d);
    free(buf);
}

rsdk_err_t rsdk_index_write(rsdk_dev_t *d, const rsdk_index_slot_t *in)
{
    if (!d || !in) return RSDK_E_PARAM;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    rsdk_index_slot_t s = *in;
    s.crc32 = 0; s.crc32 = rsdk_crc32(&s, SLOT_SZ);

    /* ★ 持锁: index_next 的 RMW + 索引槽写 必须原子, 否则 worker 写与 puller 关 writer 的 finalize
     * 并发会丢 index_next 更新 → 两段写同一索引槽 → 段索引丢失(R2)。 */
    rsdk_dev_lock(d);
    uint32_t n = st->index_slot_count;

    if (rsdk_dev_map_ready(d)) {
        /* O(1) upsert: 该 chunk 若已有 owner 槽且同 seg_id → 原地覆盖(OPEN→VALID 常用)。 */
        uint32_t i = rsdk_dev_map_get(d, in->start_chunk);
        if (i != RSDK_MAP_NONE) {
            rsdk_index_slot_t cur; rd_slot(d, i, &cur);
            if ((cur.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && cur.seg_id == in->seg_id) {
                wr_slot(d, i, &s); rsdk_rawdev_sync(rsdk_dev_raw(d));
                rsdk_dev_unlock(d);
                return RSDK_OK;
            }
        }
        /* append at index_next(环形): 覆盖旧槽前, 若旧槽是某 chunk 当前 owner 则先解除其 map 指向。 */
        uint32_t at = st->index_next;
        rsdk_index_slot_t old; rd_slot(d, at, &old);
        if (old.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) {
            rsdk_index_slot_t t = old; t.crc32 = 0;
            if (rsdk_crc32(&t, SLOT_SZ) == old.crc32 &&
                rsdk_dev_map_get(d, old.start_chunk) == at)
                rsdk_dev_map_set(d, old.start_chunk, RSDK_MAP_NONE);
        }
        wr_slot(d, at, &s);
        st->index_next = (at + 1) % n;
        rsdk_dev_map_set(d, in->start_chunk, at);   /* 新段占用该 chunk */
        rsdk_rawdev_sync(rsdk_dev_raw(d));
        rsdk_dev_flush(d);
        rsdk_dev_unlock(d);
        return RSDK_OK;
    }

    /* 回退(无 map): 向后回溯有界 upsert + append(原逻辑) */
    uint32_t scan = n < 8192 ? n : 8192;
    for (uint32_t k = 1; k <= scan; k++) {
        uint32_t i = (st->index_next + n - k) % n;
        rsdk_index_slot_t cur; rd_slot(d, i, &cur);
        if ((cur.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && cur.seg_id == in->seg_id) {
            wr_slot(d, i, &s); rsdk_rawdev_sync(rsdk_dev_raw(d));
            rsdk_dev_unlock(d);
            return RSDK_OK;
        }
        if (cur.flags == 0 && cur.seg_id == 0) break; /* 到达未写区 */
    }
    wr_slot(d, st->index_next, &s);
    st->index_next = (st->index_next + 1) % n;
    rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_flush(d);            /* 递归锁 → dev_flush 内再取同锁安全 */
    rsdk_dev_unlock(d);
    return RSDK_OK;
}

int rsdk_index_query_stream(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                            int rectype, int stream, rsdk_index_slot_t *out, int cap)
{
    if (!d || !out || cap <= 0) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (t1 == 0) t1 = 0xFFFFFFFFu;
    int found = 0;
    for (uint32_t i = 0; i < st->index_slot_count && found < cap; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        /* VALID(已封口)与 OPEN(正在写的段)都要:否则常录当前未封口的段查不到 →
         * 日历/区间"空",而回放走数据区能读到 → "接口空但播放有录像"。OPEN 段 end_time=0xFFFFFFFF,
         * 下方按 t1(查询上界=现在)当作"录到现在"。 */
        if (!(s.flags & (RSDK_SLOT_VALID | RSDK_SLOT_OPEN))) continue;
        rsdk_index_slot_t t = s; t.crc32 = 0;
        if (rsdk_crc32(&t, SLOT_SZ) != s.crc32) continue;      /* 跳损坏槽 */
        uint32_t e = (s.end_time == 0xFFFFFFFFu) ? t1 : s.end_time;
        if (e < t0 || s.start_time > t1) continue;             /* 时间不相交 */
        if (chn >= 0 && s.chn != chn) continue;
        if (rectype >= 0 && s.rectype != rectype) continue;
        if (stream >= 0 && (int)s.stream != stream) continue;
        out[found++] = s;
    }
    /* 简单按 start_time 升序 */
    for (int a = 0; a < found; a++) for (int b = a+1; b < found; b++)
        if (out[b].start_time < out[a].start_time) { rsdk_index_slot_t t=out[a]; out[a]=out[b]; out[b]=t; }
    return found;
}

int rsdk_index_query(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                     int rectype, rsdk_index_slot_t *out, int cap)
{
    return rsdk_index_query_stream(d, t0, t1, chn, rectype, -1, out, cap);
}

/* 无 cap 覆盖遍历: 批量顺序读整个索引区(不逐槽 pread), 逐个命中段回调。与 query_stream 相同的
 * 无锁+逐槽 CRC 安全模型(与录像写并发时坏/半写槽 CRC 不过→跳过)。BATCH 与 load_map 一致。 */
int rsdk_index_foreach_stream(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                              int rectype, int stream, rsdk_seg_visit_fn cb, void *user)
{
    if (!d || !cb) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (t1 == 0) t1 = 0xFFFFFFFFu;
    uint32_t n = st->index_slot_count;
    int hit = 0;

    enum { BATCH = 256 };
    rsdk_index_slot_t *buf = (rsdk_index_slot_t *)malloc(BATCH * SLOT_SZ);
    if (!buf) {                                        /* 无缓冲兜底: 逐槽读(慢但正确) */
        for (uint32_t i = 0; i < n; i++) {
            rsdk_index_slot_t s; rd_slot(d, i, &s);
            if (!(s.flags & (RSDK_SLOT_VALID | RSDK_SLOT_OPEN))) continue;
            rsdk_index_slot_t t = s; t.crc32 = 0;
            if (rsdk_crc32(&t, SLOT_SZ) != s.crc32) continue;
            uint32_t e = (s.end_time == 0xFFFFFFFFu) ? t1 : s.end_time;
            if (e < t0 || s.start_time > t1) continue;
            if (chn >= 0 && s.chn != chn) continue;
            if (rectype >= 0 && s.rectype != rectype) continue;
            if (stream >= 0 && (int)s.stream != stream) continue;
            hit++;
            if (cb(user, (int)s.chn, s.start_time, s.end_time)) return hit;
        }
        return hit;
    }
    for (uint32_t base = 0; base < n; base += BATCH) {
        uint32_t cnt = (n - base < BATCH) ? (n - base) : BATCH;
        rsdk_rawdev_pread(rsdk_dev_raw(d), slot_off(d, base), buf, cnt * SLOT_SZ);
        for (uint32_t k = 0; k < cnt; k++) {
            rsdk_index_slot_t *s = &buf[k];
            if (!(s->flags & (RSDK_SLOT_VALID | RSDK_SLOT_OPEN))) continue;
            rsdk_index_slot_t t = *s; t.crc32 = 0;
            if (rsdk_crc32(&t, SLOT_SZ) != s->crc32) continue;
            uint32_t e = (s->end_time == 0xFFFFFFFFu) ? t1 : s->end_time;
            if (e < t0 || s->start_time > t1) continue;
            if (chn >= 0 && s->chn != chn) continue;
            if (rectype >= 0 && s->rectype != rectype) continue;
            if (stream >= 0 && (int)s->stream != stream) continue;
            hit++;
            if (cb(user, (int)s->chn, s->start_time, s->end_time)) { free(buf); return hit; }
        }
    }
    free(buf);
    return hit;
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
    int cleared = 0;
    rsdk_dev_lock(d);              /* 与 index_write 互斥: 同一索引区的扫描+作废原子 */

    if (rsdk_dev_map_ready(d)) {
        /* ★ O(1): 直接取该 chunk 当前 owner 槽作废(替代全索引线性扫描 → 消除满盘每翻段的巨型停顿)。 */
        uint32_t i = rsdk_dev_map_get(d, chunk);
        if (i != RSDK_MAP_NONE) {
            rsdk_index_slot_t s; rd_slot(d, i, &s);
            if ((s.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && slot_covers_chunk(&s, chunk)) {
                s.flags = 0;
                s.crc32 = 0; s.crc32 = rsdk_crc32(&s, SLOT_SZ);
                wr_slot(d, i, &s); cleared = 1;
                rsdk_rawdev_sync(rsdk_dev_raw(d));
            }
            rsdk_dev_map_set(d, chunk, RSDK_MAP_NONE);   /* chunk 释放 */
        }
        rsdk_dev_unlock(d);
        return cleared;
    }

    /* 回退(无 map): 全索引扫描(原逻辑) */
    rsdk_systab_t *st = rsdk_dev_systab(d);
    for (uint32_t i = 0; i < st->index_slot_count; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        if ((s.flags & (RSDK_SLOT_VALID|RSDK_SLOT_OPEN)) && slot_covers_chunk(&s, chunk)) {
            s.flags = 0;                       /* 作废该段(数据已/将被覆盖) */
            s.crc32 = 0; s.crc32 = rsdk_crc32(&s, SLOT_SZ);
            wr_slot(d, i, &s); cleared++;
        }
    }
    if (cleared) rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_unlock(d);
    return cleared;
}

void rsdk_index_scan_stats(rsdk_dev_t *d, uint32_t *valid, uint32_t *open, uint32_t *corrupt)
{
    uint32_t v = 0, o = 0, c = 0;
    if (d) {
        rsdk_systab_t *st = rsdk_dev_systab(d);
        uint32_t n = st->index_slot_count;
        enum { BATCH = 256 };
        rsdk_index_slot_t *buf = (rsdk_index_slot_t *)malloc(BATCH * SLOT_SZ);
        if (buf) {
            for (uint32_t base = 0; base < n; base += BATCH) {
                uint32_t cnt = (n - base < BATCH) ? (n - base) : BATCH;
                rsdk_rawdev_pread(rsdk_dev_raw(d), slot_off(d, base), buf, cnt * SLOT_SZ);
                for (uint32_t k = 0; k < cnt; k++) {
                    rsdk_index_slot_t *s = &buf[k];
                    if (!(s->flags & (RSDK_SLOT_VALID | RSDK_SLOT_OPEN))) continue;
                    rsdk_index_slot_t t = *s; t.crc32 = 0;
                    if (rsdk_crc32(&t, SLOT_SZ) != s->crc32) { c++; continue; }
                    if (s->flags & RSDK_SLOT_OPEN) o++; else v++;
                }
            }
            free(buf);
        }
    }
    if (valid) *valid = v;
    if (open) *open = o;
    if (corrupt) *corrupt = c;
}

int rsdk_index_list_open(rsdk_dev_t *d, rsdk_index_slot_t *out, int cap)
{
    if (!d || !out || cap <= 0) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    int found = 0;
    for (uint32_t i = 0; i < st->index_slot_count && found < cap; i++) {
        rsdk_index_slot_t s; rd_slot(d, i, &s);
        if (!(s.flags & RSDK_SLOT_OPEN)) continue;
        rsdk_index_slot_t t = s; t.crc32 = 0;
        if (rsdk_crc32(&t, SLOT_SZ) != s.crc32) continue;   /* 跳损坏槽 */
        out[found++] = s;
    }
    return found;
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
