/***************************************************************************************
 *  meta_demo.c —— 元数据子系统"开发直接可用"参考程序(设计 §12 / 冻结 §5-6)。
 *
 *  演示: 原样存完整 JSON 文档(写时不解析) + 按时间戳检索 + json_extract 智能检索
 *        + 表达式索引命中 + (可选)FTS5 全文。这是 rsdk_meta.* 的最小参考实现。
 *
 *  编译:  gcc -O2 -I../include meta_demo.c -lsqlite3 -o meta_demo
 *  运行:  ./meta_demo   (在内存库跑, 无副作用)
 ***************************************************************************************/
#include "rsdk_meta.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 最小参考实现: rsdk_meta_open / put / query (基于 sqlite3) ---------- */

static int apply_schema(sqlite3 *db, const char *sql_path) {
    FILE *f = fopen(sql_path, "rb");
    if (!f) { fprintf(stderr, "找不到 schema: %s\n", sql_path); return RSDK_E_IO; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1); size_t rd = fread(buf, 1, n, f); buf[rd] = 0; fclose(f);
    char *err = NULL;
    int rc = sqlite3_exec(db, buf, NULL, NULL, &err);
    free(buf);
    if (rc != SQLITE_OK) { fprintf(stderr, "schema 失败: %s\n", err); sqlite3_free(err); return RSDK_E_DB; }
    return RSDK_OK;
}

rsdk_err_t rsdk_meta_open(const char *db_path, void **out_ctx) {
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) return RSDK_E_DB;
    *out_ctx = db;
    return RSDK_OK;
}
void rsdk_meta_close(void *ctx) { sqlite3_close((sqlite3*)ctx); }

/* 原样存: json 整块绑定为参数, SDK 不 parse */
rsdk_err_t rsdk_meta_put(void *ctx, const rsdk_meta_key_t *k,
                         const char *json, size_t len, uint64_t *doc_id) {
    sqlite3 *db = ctx;
    const char *S =
      "INSERT INTO meta_doc(ts,ts_ms,chn,event_id,doc_type,"
      "seg_disk,seg_chunk,seg_off,seg_pts,storage,enc,json_len,json) "
      "VALUES(?,?,?,?,?,?,?,?,?,0,0,?,?)";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, S, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    sqlite3_bind_int64(st, 1, k->ts);      sqlite3_bind_int(st, 2, k->ts_ms);
    sqlite3_bind_int (st, 3, k->chn);      sqlite3_bind_int64(st, 4, k->event_id);
    sqlite3_bind_int (st, 5, k->doc_type);
    sqlite3_bind_int (st, 6, k->seg.disk); sqlite3_bind_int64(st, 7, k->seg.chunk);
    sqlite3_bind_int (st, 8, k->seg.off);  sqlite3_bind_int64(st, 9, k->seg.pts);
    sqlite3_bind_int (st,10, (int)len);
    sqlite3_bind_text(st,11, json, (int)len, SQLITE_STATIC);  /* 完整 JSON 原样 */
    int rc = sqlite3_step(st);
    if (doc_id) *doc_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? RSDK_OK : RSDK_E_DB;
}

/* 通用查询打印(演示用): 打印 id/ts/seg + 完整 json 或 json_extract 结果 */
static void run_print(sqlite3 *db, const char *title, const char *sql) {
    printf("\n── %s\n   SQL: %s\n", title, sql);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        printf("   [prepare 失败: %s]\n", sqlite3_errmsg(db)); return;
    }
    int cols = sqlite3_column_count(st), rows = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        printf("   →");
        for (int i = 0; i < cols; i++)
            printf(" %s=%s", sqlite3_column_name(st, i), sqlite3_column_text(st, i));
        printf("\n"); rows++;
    }
    if (!rows) printf("   (无命中)\n");
    sqlite3_finalize(st);
}

int main(void) {
    sqlite3 *db;
    if (rsdk_meta_open(":memory:", (void**)&db) != RSDK_OK) return 1;
    if (apply_schema(db, "sql/meta_schema.sql") != RSDK_OK &&
        apply_schema(db, "../sql/meta_schema.sql") != RSDK_OK) return 1;

    printf("=== 1) 原样写入完整 JSON 文档(写时不解析) ===\n");
    /* 上层从设备取来、转成的完整结构体(与冻结 §6 的 199B 一致) */
    const char *doc =
      "{\"ts\":1784486092,\"chn\":13,\"event\":\"human\",\"rule\":5,"
      "\"objects\":[{\"id\":7,\"cls\":\"person\",\"conf\":0.92,"
      "\"bbox\":[0.12,0.08,0.26,0.64],\"color\":\"red\",\"plate\":null}],"
      "\"seg\":{\"chunk\":12040,\"off\":384,\"pts\":90000}}";
    /* 再来一条车辆+车牌, 演示车牌检索 */
    const char *doc2 =
      "{\"ts\":1784486300,\"chn\":13,\"event\":\"vehicle\",\"rule\":8,"
      "\"objects\":[{\"id\":9,\"cls\":\"vehicle\",\"conf\":0.88,"
      "\"bbox\":[0.30,0.20,0.40,0.50],\"color\":\"white\",\"plate\":\"京A12345\"}],"
      "\"seg\":{\"chunk\":12051,\"off\":128,\"pts\":18720000}}";

    rsdk_meta_key_t k1 = { .ts=1784486092, .ts_ms=320, .chn=13, .event_id=10769,
                           .doc_type=RSDK_DOC_AI_EVENT,
                           .seg={ .disk=0, .chunk=12040, .off=384, .pts=90000 } };
    rsdk_meta_key_t k2 = { .ts=1784486300, .ts_ms=0, .chn=13, .event_id=10770,
                           .doc_type=RSDK_DOC_AI_EVENT,
                           .seg={ .disk=0, .chunk=12051, .off=128, .pts=18720000 } };
    uint64_t id1=0, id2=0;
    rsdk_meta_put(db, &k1, doc,  strlen(doc),  &id1);
    rsdk_meta_put(db, &k2, doc2, strlen(doc2), &id2);
    printf("   写入 doc id=%llu(human) id=%llu(vehicle), 各存完整 JSON 原样。\n",
           (unsigned long long)id1, (unsigned long long)id2);

    printf("\n=== 2) 基础检索: 按时间戳 + 通道(走索引) ===");
    run_print(db, "查 19:00~20:00 通道13 全部文档",
      "SELECT id,ts,chn,event_id,seg_chunk,seg_off,seg_pts "
      "FROM meta_doc WHERE chn=13 AND ts BETWEEN 1784484000 AND 1784487600 ORDER BY ts");

    printf("\n=== 3) 智能检索: json_extract 深查(写入侧从未解析) ===");
    run_print(db, "查 person 且 color=red(命中 human 那条)",
      "SELECT id,ts,seg_chunk,json_extract(json,'$.objects[0].conf') AS conf "
      "FROM meta_doc WHERE chn=13 AND ts BETWEEN 1784484000 AND 1784487600 "
      "AND json_extract(json,'$.objects[0].cls')='person' "
      "AND json_extract(json,'$.objects[0].color')='red'");

    /* 前缀检索: 用"范围改写"(>=前缀 AND <前缀++)才能走表达式索引; LIKE '前缀%' 会 SCAN */
    run_print(db, "按车牌前缀 京A(范围改写, 命中 vehicle 那条)",
      "SELECT id,ts,json_extract(json,'$.objects[0].plate') AS plate "
      "FROM meta_doc WHERE storage=0 AND enc=0 "
      "AND json_extract(json,'$.objects[0].plate')>='京A' "
      "AND json_extract(json,'$.objects[0].plate')<'京B'");

    printf("\n=== 4) 证明表达式索引真的被用上(EXPLAIN QUERY PLAN) ===");
    run_print(db, "等值车牌 —— SEARCH USING INDEX ix_meta_plate",
      "EXPLAIN QUERY PLAN SELECT id FROM meta_doc WHERE storage=0 AND enc=0 "
      "AND json_extract(json,'$.objects[0].plate')='京A12345'");
    run_print(db, "前缀范围 —— 同样 SEARCH USING INDEX(而 LIKE '京A%' 会退化 SCAN)",
      "EXPLAIN QUERY PLAN SELECT id FROM meta_doc WHERE storage=0 AND enc=0 "
      "AND json_extract(json,'$.objects[0].plate')>='京A' "
      "AND json_extract(json,'$.objects[0].plate')<'京B'");

    printf("\n=== 5) 取回完整 JSON 原样(上层自己 parse) ===");
    run_print(db, "取 id=1 的完整文档",
      "SELECT id,ts,json FROM meta_doc WHERE id=1");

    printf("\n=== 6) FTS5 全文检索(若本机 sqlite 编译了 FTS5) ===\n");
    char *err = NULL;
    int rc = sqlite3_exec(db,
      "CREATE VIRTUAL TABLE meta_fts USING fts5(json, content='meta_doc', content_rowid='id');"
      "INSERT INTO meta_fts(rowid,json) SELECT id,json FROM meta_doc;", NULL, NULL, &err);
    if (rc == SQLITE_OK) {
        run_print(db, "全文匹配 'person'",
          "SELECT m.id,m.ts FROM meta_fts f JOIN meta_doc m ON m.id=f.rowid "
          "WHERE meta_fts MATCH 'person'");
    } else {
        printf("   [本机 SQLite 未启用 FTS5: %s] —— 可选特性, 不影响 §2/§3 检索。\n", err);
        sqlite3_free(err);
    }

    printf("\n=== retention: 删除某时刻前的文档(随视频覆盖调用) ===\n");
    sqlite3_exec(db, "DELETE FROM meta_doc WHERE ts < 1784486200", NULL, NULL, NULL);
    printf("   purge(ts<1784486200): 剩余 %d 条\n",
        ({ sqlite3_stmt*s; sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM meta_doc",-1,&s,0);
           sqlite3_step(s); int c=sqlite3_column_int(s,0); sqlite3_finalize(s); c; }));

    rsdk_meta_close(db);
    printf("\n完成。\n");
    return 0;
}
