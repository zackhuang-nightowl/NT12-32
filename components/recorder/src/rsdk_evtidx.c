/* Copyright (C) 2025-2026, Nightowl DG. RSDK 事件索引区实现(见 rsdk_evtidx.h)。
 * 镜像 rsdk_index.c 的环形 upsert/invalidate,元素为 128B 事件槽。
 * 事件的音视频定位/截图指针/云存态盘上权威,查询只读本区,可 rsdk_scan_rebuild 重建。 */
#include "rsdk_evtidx.h"
#include "rsdk_util.h"
#include <string.h>

#define EVT_SZ ((uint64_t)sizeof(rsdk_evt_slot_t))   /* 128 */

static uint64_t evt_off(rsdk_dev_t *d, uint32_t i) {
    return rsdk_dev_systab(d)->evtidx_start_sec * RSDK_SEC + (uint64_t)i * EVT_SZ;
}
static void rd_evt(rsdk_dev_t *d, uint32_t i, rsdk_evt_slot_t *s) {
    rsdk_rawdev_pread(rsdk_dev_raw(d), evt_off(d, i), s, EVT_SZ);
}
static void wr_evt(rsdk_dev_t *d, uint32_t i, const rsdk_evt_slot_t *s) {
    rsdk_rawdev_pwrite(rsdk_dev_raw(d), evt_off(d, i), s, EVT_SZ);
}
static uint32_t evt_crc(const rsdk_evt_slot_t *s) {
    rsdk_evt_slot_t t = *s; t.crc32 = 0;
    return rsdk_crc32(&t, EVT_SZ);
}

/* 找到 event_id 对应的槽下标(有效或未闭合);找不到返回 -1。 */
static long find_slot(rsdk_dev_t *d, uint64_t event_id) {
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint32_t n = st->evtidx_slot_count;
    for (uint32_t i = 0; i < n; i++) {
        rsdk_evt_slot_t cur; rd_evt(d, i, &cur);
        if (!(cur.flags & (RSDK_EVT_VALID | RSDK_EVT_OPEN))) continue;
        if (cur.event_id == event_id && evt_crc(&cur) == cur.crc32) return (long)i;
    }
    return -1;
}

rsdk_err_t rsdk_evtidx_write(rsdk_dev_t *d, const rsdk_evt_slot_t *in) {
    if (!d || !in) return RSDK_E_PARAM;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (st->evtidx_slot_count == 0) return RSDK_E_NOSPACE;   /* metadata=off, 无事件区 */
    rsdk_evt_slot_t s = *in;
    s.crc32 = 0; s.crc32 = rsdk_crc32(&s, EVT_SZ);

    /* 与段索引 rsdk_index_write 同锁纪律(递归 dev 锁):find+write 原子,防并发通道 evtidx_next 竞争。 */
    rsdk_dev_lock(d);
    long hit = find_slot(d, in->event_id);
    if (hit >= 0) {
        wr_evt(d, (uint32_t)hit, &s);
        rsdk_rawdev_sync(rsdk_dev_raw(d));
        rsdk_dev_unlock(d);
        return RSDK_OK;
    }
    /* append at evtidx_next(环形) */
    wr_evt(d, st->evtidx_next, &s);
    st->evtidx_next = (st->evtidx_next + 1) % st->evtidx_slot_count;
    rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_flush(d);
    rsdk_dev_unlock(d);
    return RSDK_OK;
}

rsdk_err_t rsdk_evtidx_get(rsdk_dev_t *d, uint64_t event_id, rsdk_evt_slot_t *out) {
    if (!d || !out) return RSDK_E_PARAM;
    long hit = find_slot(d, event_id);
    if (hit < 0) return RSDK_E_NOTFOUND;
    rd_evt(d, (uint32_t)hit, out);
    return RSDK_OK;
}

int rsdk_evtidx_query(rsdk_dev_t *d, uint32_t t0, uint32_t t1, int chn,
                      int state, rsdk_evt_slot_t *out, int cap) {
    if (!d || !out || cap <= 0) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    if (t1 == 0) t1 = 0xFFFFFFFFu;
    int found = 0;
    for (uint32_t i = 0; i < st->evtidx_slot_count && found < cap; i++) {
        rsdk_evt_slot_t s; rd_evt(d, i, &s);
        if (!(s.flags & (RSDK_EVT_VALID | RSDK_EVT_OPEN))) continue;
        if (evt_crc(&s) != s.crc32) continue;                 /* 跳损坏槽 */
        uint32_t e = (s.end_time == 0xFFFFFFFFu) ? t1 : s.end_time;
        if (e < t0 || s.start_time > t1) continue;            /* 时间不相交 */
        if (chn >= 0 && s.chn != chn) continue;
        if (state >= 0 && s.state != (uint8_t)state) continue;
        out[found++] = s;
    }
    /* 按 start_time 升序 */
    for (int a = 0; a < found; a++)
        for (int b = a + 1; b < found; b++)
            if (out[b].start_time < out[a].start_time) {
                rsdk_evt_slot_t t = out[a]; out[a] = out[b]; out[b] = t;
            }
    return found;
}

rsdk_err_t rsdk_evtidx_patch_snap(rsdk_dev_t *d, uint64_t event_id,
                                  uint16_t disk, uint64_t off, uint32_t len, uint32_t ts) {
    if (!d) return RSDK_E_PARAM;
    rsdk_dev_lock(d);
    long hit = find_slot(d, event_id);
    if (hit < 0) { rsdk_dev_unlock(d); return RSDK_E_NOTFOUND; }
    rsdk_evt_slot_t s; rd_evt(d, (uint32_t)hit, &s);
    s.snap_disk = disk; s.snap_off = off; s.snap_len = len; s.snap_ts = ts;
    s.flags |= RSDK_EVT_HAS_SNAP;
    s.crc32 = 0; s.crc32 = rsdk_crc32(&s, EVT_SZ);
    wr_evt(d, (uint32_t)hit, &s);
    rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_unlock(d);
    return RSDK_OK;
}

rsdk_err_t rsdk_evtidx_patch_state(rsdk_dev_t *d, uint64_t event_id,
                                   int state, int32_t err, uint32_t now) {
    if (!d) return RSDK_E_PARAM;
    rsdk_dev_lock(d);
    long hit = find_slot(d, event_id);
    if (hit < 0) { rsdk_dev_unlock(d); return RSDK_E_NOTFOUND; }
    rsdk_evt_slot_t s; rd_evt(d, (uint32_t)hit, &s);
    if (state == RSDK_CLOUD_UPLOADING) s.attempts++;   /* 转上传中记一次尝试 */
    s.state = (uint8_t)state; s.last_err = err; s.updated = now;
    s.crc32 = 0; s.crc32 = rsdk_crc32(&s, EVT_SZ);
    wr_evt(d, (uint32_t)hit, &s);
    rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_unlock(d);
    return RSDK_OK;
}

int rsdk_evtidx_invalidate_chunk(rsdk_dev_t *d, uint64_t chunk) {
    if (!d) return 0;
    rsdk_systab_t *st = rsdk_dev_systab(d);
    int cleared = 0;
    rsdk_dev_lock(d);   /* 与 evtidx_write 互斥: 同一事件区扫描+作废原子 */
    for (uint32_t i = 0; i < st->evtidx_slot_count; i++) {
        rsdk_evt_slot_t s; rd_evt(d, i, &s);
        if ((s.flags & (RSDK_EVT_VALID | RSDK_EVT_OPEN)) && s.av_chunk == chunk) {
            s.flags = 0;                       /* 作废(视频已/将被覆盖) */
            s.crc32 = 0; s.crc32 = rsdk_crc32(&s, EVT_SZ);
            wr_evt(d, i, &s); cleared++;
        }
    }
    if (cleared) rsdk_rawdev_sync(rsdk_dev_raw(d));
    rsdk_dev_unlock(d);
    return cleared;
}
