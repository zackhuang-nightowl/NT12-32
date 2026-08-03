/* Copyright (C) 2025-2026, Nightowl DG. RSDK 元数据文档库(设计 §12, 冻结 §5).
 * SQLite 后端: 原样存完整 JSON(写时不解析) + 时间戳/事件索引 + json_extract 智能检索。
 * 仅在 metadata=on 时编译进库(依赖 -lsqlite3)。 */
#include "rsdk_meta.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *SCHEMA =
"PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"
"CREATE TABLE IF NOT EXISTS meta_doc("
" id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL, ts_ms INTEGER DEFAULT 0,"
" chn INTEGER, event_id INTEGER, doc_type INTEGER,"
" seg_disk INTEGER, seg_chunk INTEGER, seg_off INTEGER, seg_pts INTEGER,"
" storage INTEGER DEFAULT 0, enc INTEGER DEFAULT 0, json_len INTEGER, json TEXT,"
" meta_disk INTEGER, meta_off INTEGER, meta_len INTEGER);"
"CREATE INDEX IF NOT EXISTS ix_meta_ts ON meta_doc(ts);"
"CREATE INDEX IF NOT EXISTS ix_meta_ce ON meta_doc(chn,event_id);"
"CREATE INDEX IF NOT EXISTS ix_meta_et ON meta_doc(event_id);"
"CREATE INDEX IF NOT EXISTS ix_meta_dt ON meta_doc(doc_type,ts);"
"CREATE INDEX IF NOT EXISTS ix_meta_plate ON meta_doc(json_extract(json,'$.objects[0].plate')) WHERE storage=0 AND enc=0;"
"CREATE INDEX IF NOT EXISTS ix_meta_cls   ON meta_doc(json_extract(json,'$.objects[0].cls'))   WHERE storage=0 AND enc=0;";

rsdk_err_t rsdk_meta_open(const char *db_path, void **out_ctx) {
    if (!db_path || !out_ctx) return RSDK_E_PARAM;
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) return RSDK_E_DB;
    if (sqlite3_exec(db, SCHEMA, NULL, NULL, NULL) != SQLITE_OK) { sqlite3_close(db); return RSDK_E_DB; }
    *out_ctx = db;
    return RSDK_OK;
}
void rsdk_meta_close(void *ctx) { if (ctx) sqlite3_close((sqlite3*)ctx); }

void *rsdk_meta_db(void *ctx) { return ctx; }   /* ctx 即 sqlite3*（见 rsdk_meta_open） */

rsdk_err_t rsdk_meta_put(void *ctx, const rsdk_meta_key_t *k,
                         const char *json, size_t len, uint64_t *doc_id) {
    if (!ctx || !k || !json) return RSDK_E_PARAM;
    sqlite3 *db = ctx; sqlite3_stmt *st;
    const char *S = "INSERT INTO meta_doc(ts,ts_ms,chn,event_id,doc_type,"
        "seg_disk,seg_chunk,seg_off,seg_pts,storage,enc,json_len,json)"
        " VALUES(?,?,?,?,?,?,?,?,?,0,0,?,?)";
    if (sqlite3_prepare_v2(db, S, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    sqlite3_bind_int64(st,1,k->ts);   sqlite3_bind_int(st,2,k->ts_ms);
    sqlite3_bind_int(st,3,k->chn);    sqlite3_bind_int64(st,4,(sqlite3_int64)k->event_id);
    sqlite3_bind_int(st,5,k->doc_type);
    sqlite3_bind_int(st,6,k->seg.disk); sqlite3_bind_int64(st,7,(sqlite3_int64)k->seg.chunk);
    sqlite3_bind_int(st,8,k->seg.off);  sqlite3_bind_int64(st,9,(sqlite3_int64)k->seg.pts);
    sqlite3_bind_int(st,10,(int)len);
    sqlite3_bind_text(st,11,json,(int)len,SQLITE_STATIC);
    int rc = sqlite3_step(st);
    if (doc_id) *doc_id = (uint64_t)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? RSDK_OK : RSDK_E_DB;
}

rsdk_err_t rsdk_meta_query(void *ctx, const rsdk_meta_query_t *q, rsdk_metadoc_list_t *out) {
    if (!ctx || !q || !out) return RSDK_E_PARAM;
    sqlite3 *db = ctx;
    char sql[1024];
    snprintf(sql, sizeof sql,
      "SELECT id,ts,ts_ms,chn,event_id,doc_type,seg_disk,seg_chunk,seg_off,seg_pts,json "
      "FROM meta_doc WHERE ts BETWEEN %u AND %u%s%s%s%s ORDER BY ts %s",
      q->t0, q->t1 ? q->t1 : 0xFFFFFFFFu,
      q->chn >= 0 ? " AND chn=?1" : "",
      q->event_id ? " AND event_id=?2" : "",
      q->doc_type ? " AND doc_type=?3" : "",
      (q->json_path && q->json_match) ? " AND json_extract(json,?4) LIKE ?5" : "",
      q->limit ? "LIMIT ?6" : "");
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    if (q->chn >= 0)   sqlite3_bind_int(st,1,q->chn);
    if (q->event_id)   sqlite3_bind_int64(st,2,(sqlite3_int64)q->event_id);
    if (q->doc_type)   sqlite3_bind_int(st,3,q->doc_type);
    if (q->json_path && q->json_match) {
        sqlite3_bind_text(st,4,q->json_path,-1,SQLITE_STATIC);
        sqlite3_bind_text(st,5,q->json_match,-1,SQLITE_STATIC);
    }
    if (q->limit) sqlite3_bind_int(st,6,q->limit);

    int cap = 16, n = 0;
    rsdk_metadoc_t *arr = calloc(cap, sizeof *arr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) { cap *= 2; arr = realloc(arr, cap * sizeof *arr); }
        rsdk_metadoc_t *m = &arr[n++];
        memset(m, 0, sizeof *m);
        m->id = (uint64_t)sqlite3_column_int64(st,0);
        m->key.ts = (uint32_t)sqlite3_column_int64(st,1);
        m->key.ts_ms = (uint16_t)sqlite3_column_int(st,2);
        m->key.chn = (int16_t)sqlite3_column_int(st,3);
        m->key.event_id = (uint64_t)sqlite3_column_int64(st,4);
        m->key.doc_type = (uint32_t)sqlite3_column_int(st,5);
        m->key.seg.disk = (uint16_t)sqlite3_column_int(st,6);
        m->key.seg.chunk = (uint64_t)sqlite3_column_int64(st,7);
        m->key.seg.off = (uint32_t)sqlite3_column_int(st,8);
        m->key.seg.pts = (uint64_t)sqlite3_column_int64(st,9);
        const char *j = (const char*)sqlite3_column_text(st,10);
        int jl = sqlite3_column_bytes(st,10);
        char *dup = malloc(jl + 1); memcpy(dup, j ? j : "", jl); dup[jl] = 0;
        m->json = dup; m->json_len = (size_t)jl;
    }
    sqlite3_finalize(st);
    out->docs = arr; out->count = n;
    return RSDK_OK;
}

rsdk_err_t rsdk_meta_get(void *ctx, uint64_t doc_id, rsdk_metadoc_t *out) {
    rsdk_meta_query_t q; memset(&q, 0, sizeof q); q.t0 = 0; q.t1 = 0xFFFFFFFFu; q.chn = -1;
    /* 简化: 用 event_id 通道不便, 直接 SQL by id */
    sqlite3 *db = ctx; sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,"SELECT ts,chn,event_id,doc_type,json FROM meta_doc WHERE id=?",-1,&st,NULL)!=SQLITE_OK)
        return RSDK_E_DB;
    sqlite3_bind_int64(st,1,(sqlite3_int64)doc_id);
    rsdk_err_t rc = RSDK_E_NOTFOUND;
    if (sqlite3_step(st) == SQLITE_ROW) {
        memset(out,0,sizeof *out); out->id = doc_id;
        out->key.ts=(uint32_t)sqlite3_column_int64(st,0); out->key.chn=(int16_t)sqlite3_column_int(st,1);
        out->key.event_id=(uint64_t)sqlite3_column_int64(st,2); out->key.doc_type=(uint32_t)sqlite3_column_int(st,3);
        const char *j=(const char*)sqlite3_column_text(st,4); int jl=sqlite3_column_bytes(st,4);
        char *dup=malloc(jl+1); memcpy(dup,j?j:"",jl); dup[jl]=0; out->json=dup; out->json_len=(size_t)jl;
        rc = RSDK_OK;
    }
    sqlite3_finalize(st);
    return rc;
}

int rsdk_meta_purge(void *ctx, uint32_t before) {
    sqlite3 *db = ctx; sqlite3_stmt *st;
    sqlite3_prepare_v2(db,"DELETE FROM meta_doc WHERE ts < ?",-1,&st,NULL);
    sqlite3_bind_int64(st,1,before); sqlite3_step(st); sqlite3_finalize(st);
    return sqlite3_changes(db);
}

int rsdk_meta_purge_chunk(void *ctx, int disk, uint64_t chunk) {
    sqlite3 *db = ctx; sqlite3_stmt *st;
    sqlite3_prepare_v2(db,"DELETE FROM meta_doc WHERE seg_disk=? AND seg_chunk=?",-1,&st,NULL);
    sqlite3_bind_int(st,1,disk); sqlite3_bind_int64(st,2,(sqlite3_int64)chunk);
    sqlite3_step(st); sqlite3_finalize(st);
    return sqlite3_changes(db);
}

void rsdk_meta_free_list(rsdk_metadoc_list_t *lst) {
    if (!lst || !lst->docs) return;
    for (int i = 0; i < lst->count; i++) free((void*)lst->docs[i].json);
    free(lst->docs); lst->docs = NULL; lst->count = 0;
}
