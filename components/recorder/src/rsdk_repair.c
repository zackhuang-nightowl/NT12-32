/* Copyright (C) 2025-2026, Nightowl DG. RSDK 开机自检 + 分级修复。见 rsdk_repair.h。 */
#include "rsdk_repair.h"
#include "rsdk_balance.h"
#include "rsdk_storgedev.h"
#include "rsdk_index.h"
#include "rsdk_scan.h"
#include "rsdk_feature.h"     /* RSDK_CFG_METADATA */
#include "rsdk_util.h"        /* rsdk_crc32 */
#if RSDK_CFG_METADATA
#include "rsdk_meta.h"        /* rsdk_meta_db */
#include <sqlite3.h>
#endif
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ---- 分类 ---- */
typedef enum { CLS_HEALTHY = 0, CLS_LIGHT, CLS_FULL } cls_t;

#define FULL_FRAC_NUM 1
#define FULL_FRAC_DEN 2        /* 有效段 < 应有段 × 1/2 → 判缺失 */
#define CORRUPT_NUM   1
#define CORRUPT_DEN   4        /* 坏槽占比 > 1/4 → 判不可信 */

/* 探针:读 chunk 首帧头,校验 magic + hdr CRC,确认数据区确有真帧。 */
static int probe_has_frame(rsdk_dev_t *d, uint64_t chunk)
{
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    if (!sb) return 0;
    uint64_t off = (sb->data_start_sec + chunk * sb->chunk_sectors) * RSDK_SEC;
    uint8_t hdr[64];
    if (rsdk_rawdev_pread(rsdk_dev_raw(d), off, hdr, 64) != RSDK_OK) return 0;
    rsdk_frame_hdr_t *h = (rsdk_frame_hdr_t *)hdr;
    if (memcmp(h->magic, RSDK_FRAME_MAGIC, 8) != 0) return 0;
    uint32_t crc = h->hdr_crc32; h->hdr_crc32 = 0;
    return rsdk_crc32(hdr, 64) == crc;
}

static cls_t classify(rsdk_dev_t *d)
{
    uint32_t valid = 0, open = 0, corrupt = 0;
    rsdk_index_scan_stats(d, &valid, &open, &corrupt);
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    if (!sb) return CLS_HEALTHY;

    int wrapped = rsdk_dev_is_wrapped(d);
    uint64_t data_chunks = (sb->chunk_count > sb->meta_chunk_count)
                         ? sb->chunk_count - sb->meta_chunk_count : 0;
    uint64_t expected = wrapped ? data_chunks : sb->write_ptr_chunk;

    int data_present = (sb->write_ptr_chunk > 0 || wrapped);
    if (data_present) {
        uint64_t last = (!wrapped && sb->write_ptr_chunk > 0) ? sb->write_ptr_chunk - 1 : 0;
        data_present = probe_has_frame(d, last) || probe_has_frame(d, 0);
    }

    uint64_t good = (uint64_t)valid + open;
    uint32_t flagged = valid + open + corrupt;

    /* 索引不可信:坏槽占比过高 */
    if (flagged > 0 && (uint64_t)corrupt * CORRUPT_DEN > (uint64_t)flagged * CORRUPT_NUM)
        return CLS_FULL;
    /* 索引丢失/严重落后:有数据但有效段远少于应有段 */
    if (data_present && expected > 0 && good * FULL_FRAC_DEN < expected * FULL_FRAC_NUM)
        return CLS_FULL;
    if (open > 0 || corrupt > 0) return CLS_LIGHT;
    return CLS_HEALTHY;
}

#if RSDK_CFG_METADATA
static int meta_is_empty(void *meta)
{
    sqlite3 *db = (sqlite3 *)rsdk_meta_db(meta);
    if (!db) return 0;
    sqlite3_stmt *st = NULL;
    int empty = 1;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM meta_doc LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) empty = 0;
        sqlite3_finalize(st);
    }
    return empty;
}
#endif

/* ---- 后台重建任务(单任务;本机同一时刻只有一个盘组) ---- */
static struct {
    rsdk_group_t *g;
    pthread_t     th;
    int           th_ok;
    volatile int  status;    /* rsdk_repair_status_t */
    volatile int  percent;
    volatile int  abort;
    void         *meta;
    int           pass_meta;
    unsigned      scan_mask;
    uint64_t      total_chunks;
    uint64_t      done_base;
} J;
static pthread_mutex_t J_lock = PTHREAD_MUTEX_INITIALIZER;

static void prog_cb(void *user, uint64_t done, uint64_t total)
{
    (void)user; (void)total;
    uint64_t tot = J.total_chunks ? J.total_chunks : 1;
    uint64_t cur = J.done_base + done;
    int pct = (int)((cur * 100) / tot);
    J.percent = pct > 100 ? 100 : pct;
}

static void *rebuild_thread(void *arg)
{
    (void)arg;
    int ndisk = rsdk_group_count(J.g);
    rsdk_err_t rc = RSDK_OK;
    for (int i = 0; i < ndisk && !J.abort; i++) {
        if (!(J.scan_mask & (1u << i))) continue;
        rsdk_dev_t *d = rsdk_group_dev(J.g, i);
        if (!d) continue;
        void *m = J.pass_meta ? J.meta : NULL;
        rsdk_err_t r = rsdk_scan_rebuild2(d, m, NULL, NULL, prog_cb, NULL, &J.abort);
        if (r != RSDK_OK) rc = r;
        rsdk_superblock_t *sb = rsdk_dev_sb(d);
        J.done_base += sb ? sb->chunk_count : 0;
    }
    J.status = J.abort ? RSDK_REPAIR_FAILED
                       : (rc == RSDK_OK ? RSDK_REPAIR_DONE : RSDK_REPAIR_FAILED);
    fprintf(stderr, "[rsdk_repair] 后台重建结束: %s\n",
            J.status == RSDK_REPAIR_DONE ? "OK" : "FAILED/ABORT");
    return NULL;
}

rsdk_err_t rsdk_group_check_and_repair(rsdk_group_t *g, void *meta)
{
    if (!g) return RSDK_E_PARAM;
    int ndisk = rsdk_group_count(g);
    unsigned scan_mask = 0;
    uint64_t total_chunks = 0;
    int any_data = 0;

    /* 逐盘分类;FULL 记入后台扫描,其余同步 Tier1 封口。 */
    for (int i = 0; i < ndisk; i++) {
        rsdk_dev_t *d = rsdk_group_dev(g, i);
        if (!d) continue;
        rsdk_superblock_t *sb = rsdk_dev_sb(d);
        if (sb && (sb->write_ptr_chunk > 0 || rsdk_dev_is_wrapped(d))) any_data = 1;

        cls_t cls = classify(d);
        if (cls == CLS_FULL) {
            scan_mask |= (1u << i);
            total_chunks += sb ? sb->chunk_count : 0;
            fprintf(stderr, "[rsdk_repair] disk%d 索引丢失/损坏 → 后台全盘重建\n", i);
        } else {
            int fixed = 0;
            rsdk_scan_finalize_open(d, &fixed);            /* Tier1:无 OPEN 则 no-op */
            if (fixed) fprintf(stderr, "[rsdk_repair] disk%d 封口 %d 个未闭合段\n", i, fixed);
        }
    }

    int pass_meta = 0;
#if RSDK_CFG_METADATA
    if (meta && any_data && meta_is_empty(meta)) {
        pass_meta = 1;                                     /* meta 空 → 后台数据扫描顺带回填事件 */
        for (int i = 0; i < ndisk; i++) {
            rsdk_dev_t *d = rsdk_group_dev(g, i);
            if (!d) continue;
            rsdk_superblock_t *sb = rsdk_dev_sb(d);
            int dp = sb && (sb->write_ptr_chunk > 0 || rsdk_dev_is_wrapped(d));
            if (dp && !(scan_mask & (1u << i))) {
                scan_mask |= (1u << i);
                total_chunks += sb ? sb->chunk_count : 0;
            }
        }
        fprintf(stderr, "[rsdk_repair] meta 空但有录像 → 后台扫描回填事件\n");
    }
#endif

    if (!scan_mask) return RSDK_OK;                        /* 仅 Tier1,已同步完成 */

    pthread_mutex_lock(&J_lock);
    if (J.th_ok) { pthread_mutex_unlock(&J_lock); return RSDK_OK; }  /* 已有后台任务 */
    memset(&J, 0, sizeof J);
    J.g = g; J.meta = meta; J.pass_meta = pass_meta; J.scan_mask = scan_mask;
    J.total_chunks = total_chunks; J.done_base = 0; J.abort = 0;
    J.status = RSDK_REPAIR_RUNNING; J.percent = 0;
    if (pthread_create(&J.th, NULL, rebuild_thread, NULL) == 0) J.th_ok = 1;
    else J.status = RSDK_REPAIR_FAILED;
    pthread_mutex_unlock(&J_lock);
    return RSDK_OK;
}

rsdk_repair_status_t rsdk_group_repair_progress(rsdk_group_t *g, int *percent)
{
    rsdk_repair_status_t s = RSDK_REPAIR_IDLE;
    pthread_mutex_lock(&J_lock);
    if (J.g == g && J.th_ok) {
        s = (rsdk_repair_status_t)J.status;
        if (percent) *percent = J.percent;
    } else if (percent) {
        *percent = 0;
    }
    pthread_mutex_unlock(&J_lock);
    return s;
}

void rsdk_group_repair_stop(rsdk_group_t *g)
{
    pthread_mutex_lock(&J_lock);
    if (J.g == g && J.th_ok) {
        J.abort = 1;
        pthread_t th = J.th;
        J.th_ok = 0;
        pthread_mutex_unlock(&J_lock);
        pthread_join(th, NULL);
        return;
    }
    pthread_mutex_unlock(&J_lock);
}
