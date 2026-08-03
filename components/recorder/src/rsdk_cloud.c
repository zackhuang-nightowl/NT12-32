/* Copyright (C) 2025-2026, Nightowl DG. rsdk_cloud — 云存上传状态（meta.db doc_type=CLOUD）。
 * 仅在 metadata=on 时编译。见 rsdk_cloud.h / 计划 §B5。 */
#include "rsdk_cloud.h"
#include "rsdk_meta.h"    /* RSDK_DOC_CLOUD, rsdk_meta_db */

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#define DOC_CLOUD RSDK_DOC_CLOUD   /* = 8 */

static sqlite3 *db_of(void *meta) { return (sqlite3 *)rsdk_meta_db(meta); }

uint64_t rsdk_cloud_make_event_id(int chn, uint32_t starttime, int rectype, uint16_t salt)
{
    uint64_t id = 0;
    id |= ((uint64_t)(chn     & 0xFF)) << 56;
    id |= ((uint64_t)(rectype & 0xFF)) << 48;
    id |= ((uint64_t)(salt    & 0xFFFF)) << 32;
    id |=  (uint64_t)starttime;
    return id;
}

rsdk_err_t rsdk_cloud_event_begin(void *meta, const rsdk_cloud_event_t *ev)
{
    sqlite3 *db = db_of(meta);
    if (!db || !ev) return RSDK_E_DB;

    /* 幂等 upsert：先 UPDATE，无行则 INSERT。json 为初始文档（state=ev->state 或 PENDING）。 */
    int state = ev->state ? (int)ev->state : RSDK_CLOUD_PENDING;
    char json[512];
    snprintf(json, sizeof(json),
        "{\"state\":%d,\"rectype\":%u,\"starttime\":%u,\"end\":%u,\"seg_id\":%u,"
        "\"disk\":%u,\"start_chunk\":%llu,\"attempts\":%u,\"last_err\":%d,\"segs\":[]}",
        state, ev->rectype, ev->starttime, ev->end_time, ev->seg_id,
        (unsigned)ev->disk, (unsigned long long)ev->start_chunk, ev->attempts, ev->last_err);

    sqlite3_stmt *st = NULL;
    /* UPDATE 现有行（保留已积累的 segs / attempts，只刷起始信息与 end/seg_id） */
    const char *U =
        "UPDATE meta_doc SET ts=?, chn=?, "
        " json=json_set(json,'$.rectype',?,'$.starttime',?,'$.end',?,'$.seg_id',?,'$.disk',?,'$.start_chunk',?) "
        "WHERE doc_type=? AND event_id=?;";
    if (sqlite3_prepare_v2(db, U, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    int c = 1;
    sqlite3_bind_int64(st, c++, ev->starttime);
    sqlite3_bind_int  (st, c++, ev->chn);
    sqlite3_bind_int  (st, c++, (int)ev->rectype);
    sqlite3_bind_int64(st, c++, ev->starttime);
    sqlite3_bind_int64(st, c++, ev->end_time);
    sqlite3_bind_int  (st, c++, (int)ev->seg_id);
    sqlite3_bind_int  (st, c++, (int)ev->disk);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)ev->start_chunk);
    sqlite3_bind_int  (st, c++, DOC_CLOUD);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)ev->event_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return RSDK_E_DB;
    if (sqlite3_changes(db) > 0) return RSDK_OK;

    /* 无行 → INSERT 初始文档 */
    const char *I =
        "INSERT INTO meta_doc(ts,ts_ms,chn,event_id,doc_type,seg_disk,seg_chunk,seg_off,seg_pts,"
        "storage,enc,json_len,json) VALUES(?,0,?,?,?,?,?,0,0,0,0,?,?);";
    if (sqlite3_prepare_v2(db, I, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    c = 1;
    sqlite3_bind_int64(st, c++, ev->starttime);
    sqlite3_bind_int  (st, c++, ev->chn);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)ev->event_id);
    sqlite3_bind_int  (st, c++, DOC_CLOUD);
    sqlite3_bind_int  (st, c++, (int)ev->disk);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)ev->start_chunk);
    sqlite3_bind_int  (st, c++, (int)strlen(json));
    sqlite3_bind_text (st, c++, json, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? RSDK_OK : RSDK_E_DB;
}

rsdk_err_t rsdk_cloud_event_add_seg(void *meta, uint64_t event_id, const rsdk_index_slot_t *seg)
{
    sqlite3 *db = db_of(meta);
    if (!db || !seg) return RSDK_E_DB;
    sqlite3_stmt *st = NULL;
    /* 追加一个段对象到 $.segs，并把 end 更新为该段 end_time */
    const char *U =
        "UPDATE meta_doc SET "
        " json=json_set(json_insert(json,'$.segs[#]',"
        "   json_object('disk',?,'chunk',?,'seg_id',?)),'$.end',?) "
        "WHERE doc_type=? AND event_id=?;";
    if (sqlite3_prepare_v2(db, U, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    int c = 1;
    sqlite3_bind_int  (st, c++, (int)seg->start_disk);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)seg->start_chunk);
    sqlite3_bind_int  (st, c++, (int)seg->seg_id);
    sqlite3_bind_int64(st, c++, (seg->end_time == 0xFFFFFFFFu) ? 0 : seg->end_time);
    sqlite3_bind_int  (st, c++, DOC_CLOUD);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)event_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? RSDK_OK : RSDK_E_DB;
}

rsdk_err_t rsdk_cloud_set_state(void *meta, uint64_t event_id, rsdk_cloud_state_t stt, int32_t err)
{
    sqlite3 *db = db_of(meta);
    if (!db) return RSDK_E_DB;
    sqlite3_stmt *st = NULL;
    /* 转 UPLOADING 视为一次新尝试 → attempts++ */
    const char *U =
        "UPDATE meta_doc SET json=json_set(json,'$.state',?,'$.last_err',?,'$.updated',strftime('%s','now'),"
        " '$.attempts', json_extract(json,'$.attempts') + ?) "
        "WHERE doc_type=? AND event_id=?;";
    if (sqlite3_prepare_v2(db, U, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    int c = 1;
    sqlite3_bind_int(st, c++, (int)stt);
    sqlite3_bind_int(st, c++, err);
    sqlite3_bind_int(st, c++, (stt == RSDK_CLOUD_UPLOADING) ? 1 : 0);
    sqlite3_bind_int(st, c++, DOC_CLOUD);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)event_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? RSDK_OK : RSDK_E_DB;
}

static void fill_event(sqlite3_stmt *st, rsdk_cloud_event_t *o)
{
    /* 列序: event_id, chn, ts, state, rectype, starttime, end, seg_id, disk, start_chunk, attempts, last_err, updated */
    memset(o, 0, sizeof(*o));
    o->event_id   = (uint64_t)sqlite3_column_int64(st, 0);
    o->chn        = sqlite3_column_int(st, 1);
    o->starttime  = (uint32_t)sqlite3_column_int64(st, 2);
    o->state      = (rsdk_cloud_state_t)sqlite3_column_int(st, 3);
    o->rectype    = (uint32_t)sqlite3_column_int(st, 4);
    /* col5 starttime(冗余) 跳过 */
    o->end_time   = (uint32_t)sqlite3_column_int64(st, 6);
    o->seg_id     = (uint32_t)sqlite3_column_int64(st, 7);
    o->disk       = (uint16_t)sqlite3_column_int(st, 8);
    o->start_chunk= (uint64_t)sqlite3_column_int64(st, 9);
    o->attempts   = (uint32_t)sqlite3_column_int64(st, 10);
    o->last_err   = sqlite3_column_int(st, 11);
    o->updated_ts = (uint32_t)sqlite3_column_int64(st, 12);
}

#define CLOUD_COLS \
    "event_id,chn,ts," \
    "json_extract(json,'$.state'),json_extract(json,'$.rectype'),json_extract(json,'$.starttime')," \
    "json_extract(json,'$.end'),json_extract(json,'$.seg_id'),json_extract(json,'$.disk')," \
    "json_extract(json,'$.start_chunk'),json_extract(json,'$.attempts'),json_extract(json,'$.last_err')," \
    "coalesce(json_extract(json,'$.updated'),0)"

rsdk_err_t rsdk_cloud_get(void *meta, uint64_t event_id, rsdk_cloud_event_t *out)
{
    sqlite3 *db = db_of(meta);
    if (!db || !out) return RSDK_E_DB;
    sqlite3_stmt *st = NULL;
    const char *S = "SELECT " CLOUD_COLS " FROM meta_doc WHERE doc_type=? AND event_id=?;";
    if (sqlite3_prepare_v2(db, S, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    sqlite3_bind_int(st, 1, DOC_CLOUD);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)event_id);
    rsdk_err_t rc = RSDK_E_NOTFOUND;
    if (sqlite3_step(st) == SQLITE_ROW) { fill_event(st, out); rc = RSDK_OK; }
    sqlite3_finalize(st);
    return rc;
}

int rsdk_cloud_enumerate_pending(void *meta, const rsdk_cloud_poll_opt_t *opt,
                                 rsdk_cloud_event_t *out, int cap)
{
    sqlite3 *db = db_of(meta);
    if (!db || !out || cap <= 0) return -1;

    int inc_failed = opt ? opt->include_failed : 1;
    uint32_t stale = opt ? opt->stale_uploading_s : 0;
    int chn = opt ? opt->chn : -1;

    /* state ∈ {PENDING} [∪ FAILED] [∪ 过期 UPLOADING] */
    char sql[768];
    snprintf(sql, sizeof(sql),
        "SELECT " CLOUD_COLS " FROM meta_doc WHERE doc_type=%d AND ("
        " json_extract(json,'$.state')=%d"
        "%s"
        "%s"
        ")%s ORDER BY ts LIMIT %d;",
        DOC_CLOUD,
        RSDK_CLOUD_PENDING,
        inc_failed ? " OR json_extract(json,'$.state')=4" : "",
        stale ? " OR (json_extract(json,'$.state')=2 AND coalesce(json_extract(json,'$.updated'),0) < (strftime('%s','now')-?))" : "",
        (chn >= 0) ? " AND chn=?" : "",
        cap);

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int b = 1;
    if (stale)      sqlite3_bind_int(st, b++, (int)stale);
    if (chn >= 0)   sqlite3_bind_int(st, b++, chn);

    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < cap) fill_event(st, &out[n++]);
    sqlite3_finalize(st);
    return n;
}

int rsdk_cloud_on_reclaim(void *meta, uint16_t disk, uint64_t chunk)
{
    sqlite3 *db = db_of(meta);
    if (!db) return -1;
    sqlite3_stmt *st = NULL;
    /* 起始 chunk 命中，或 segs 中任一段命中；且未 DONE/LOST → 置 LOST */
    const char *U =
        "UPDATE meta_doc SET json=json_set(json,'$.state',?) "
        "WHERE doc_type=? AND json_extract(json,'$.state') NOT IN (3,5) AND ("
        "  (json_extract(json,'$.disk')=? AND json_extract(json,'$.start_chunk')=?)"
        "  OR EXISTS(SELECT 1 FROM json_each(json,'$.segs') je "
        "            WHERE json_extract(je.value,'$.disk')=? AND json_extract(je.value,'$.chunk')=?)"
        ");";
    if (sqlite3_prepare_v2(db, U, -1, &st, NULL) != SQLITE_OK) return -1;
    int c = 1;
    sqlite3_bind_int  (st, c++, RSDK_CLOUD_LOST);
    sqlite3_bind_int  (st, c++, DOC_CLOUD);
    sqlite3_bind_int  (st, c++, (int)disk);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)chunk);
    sqlite3_bind_int  (st, c++, (int)disk);
    sqlite3_bind_int64(st, c++, (sqlite3_int64)chunk);
    int rc = sqlite3_step(st);
    int changed = (rc == SQLITE_DONE) ? sqlite3_changes(db) : -1;
    sqlite3_finalize(st);
    return changed;
}
