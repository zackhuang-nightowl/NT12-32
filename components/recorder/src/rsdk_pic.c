/* Copyright (C) 2025-2026, Nightowl DG. RSDK 抓拍(PIC).
 * 事件触发 JPEG → 加密写 MetaRegion → 索引进 meta_doc(SNAP, 绑 event_id) → 供事件推送取图。
 * 仅在 metadata=on 时编译(依赖 sqlite3 + MetaRegion)。 */
#include "rsdk_pic.h"
#include "rsdk_meta.h"      /* RSDK_DOC_SNAP */
#include "rsdk_crypto.h"
#include "rsdk_util.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void pic_xcrypt(rsdk_dev_t *d, uint64_t abs_off, uint8_t *buf, size_t len, int *enc) {
    struct rsdk_crypto *cr = rsdk_dev_crypto(d);
    if (cr && len) {
        rsdk_crypto_xcrypt(cr, (uint32_t)abs_off, (uint32_t)(abs_off >> 32), 0, buf, len);
        *enc = 1;
    } else *enc = 0;
}

rsdk_err_t rsdk_pic_write(rsdk_dev_t *d, void *meta, const rsdk_pic_key_t *k,
                          const void *jpeg, size_t len, uint64_t *pic_id)
{
    if (!d || !meta || !k || !jpeg || !len) return RSDK_E_PARAM;
    sqlite3 *db = meta;

    /* 分配 MetaRegion 空间(头 + JPEG) */
    uint64_t total = sizeof(rsdk_pic_hdr_t) + len, abs_off;
    rsdk_err_t rc = rsdk_dev_meta_alloc(d, total, &abs_off);
    if (rc) return rc;                          /* metadata=off → E_NOSPACE */

    /* 拷贝并(按盘加密)负载 */
    uint8_t *buf = malloc(len); if (!buf) return RSDK_E_IO;
    memcpy(buf, jpeg, len);
    int enc = 0;
    pic_xcrypt(d, abs_off, buf, len, &enc);

    /* 头 */
    rsdk_pic_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.magic, "PIC0", 4);
    h.type = (uint8_t)k->type; h.enc = (uint8_t)enc; h.chn = (uint16_t)k->chn;
    h.event_id = k->event_id; h.ts = k->ts; h.w = k->w; h.h = k->h; h.jpeg_len = (uint32_t)len;
    h.crc32 = 0; h.crc32 = rsdk_crc32(&h, sizeof h);

    rsdk_rawdev_t *raw = rsdk_dev_raw(d);
    rsdk_rawdev_pwrite(raw, abs_off, &h, sizeof h);
    rsdk_rawdev_pwrite(raw, abs_off + sizeof h, buf, len);
    rsdk_rawdev_sync(raw);
    free(buf);

    /* 索引: meta_doc SNAP 行(storage=1 blobref), 绑 event_id */
    char json[96];
    int jl = snprintf(json, sizeof json, "{\"pic\":%d,\"w\":%u,\"h\":%u}", k->type, k->w, k->h);
    const char *S = "INSERT INTO meta_doc(ts,ts_ms,chn,event_id,doc_type,"
        "seg_disk,seg_chunk,seg_off,seg_pts,storage,enc,json_len,json,meta_disk,meta_off,meta_len)"
        " VALUES(?,0,?,?,?,?,?,?,?,1,?,?,?,?,?,?)";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, S, -1, &st, NULL) != SQLITE_OK) return RSDK_E_DB;
    sqlite3_bind_int64(st,1,k->ts); sqlite3_bind_int(st,2,k->chn);
    sqlite3_bind_int64(st,3,(sqlite3_int64)k->event_id); sqlite3_bind_int(st,4,RSDK_DOC_SNAP);
    sqlite3_bind_int(st,5,k->seg.disk); sqlite3_bind_int64(st,6,(sqlite3_int64)k->seg.chunk);
    sqlite3_bind_int(st,7,k->seg.off); sqlite3_bind_int64(st,8,(sqlite3_int64)k->seg.pts);
    sqlite3_bind_int(st,9,enc); sqlite3_bind_int(st,10,jl); sqlite3_bind_text(st,11,json,jl,SQLITE_STATIC);
    sqlite3_bind_int(st,12,rsdk_dev_index(d));
    sqlite3_bind_int64(st,13,(sqlite3_int64)abs_off); sqlite3_bind_int(st,14,(int)total);
    int r = sqlite3_step(st);
    if (pic_id) *pic_id = (uint64_t)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);
    return r == SQLITE_DONE ? RSDK_OK : RSDK_E_DB;
}

static rsdk_err_t read_blob(rsdk_dev_t *d, uint64_t off, void **jpeg, size_t *len) {
    rsdk_pic_hdr_t h;
    rsdk_rawdev_pread(rsdk_dev_raw(d), off, &h, sizeof h);
    if (memcmp(h.magic, "PIC0", 4) != 0) return RSDK_E_CORRUPT;
    rsdk_pic_hdr_t t = h; t.crc32 = 0;
    if (rsdk_crc32(&t, sizeof t) != h.crc32) return RSDK_E_CORRUPT;
    uint8_t *buf = malloc(h.jpeg_len); if (!buf) return RSDK_E_IO;
    rsdk_rawdev_pread(rsdk_dev_raw(d), off + sizeof h, buf, h.jpeg_len);
    if (h.enc) {
        struct rsdk_crypto *cr = rsdk_dev_crypto(d);
        if (!cr) { free(buf); return RSDK_E_CRYPTO; }
        rsdk_crypto_xcrypt(cr, (uint32_t)off, (uint32_t)(off >> 32), 0, buf, h.jpeg_len);
    }
    *jpeg = buf; *len = h.jpeg_len;
    return RSDK_OK;
}

rsdk_err_t rsdk_pic_read(rsdk_dev_t *d, void *meta, uint64_t pic_id, void **jpeg, size_t *len) {
    if (!d || !meta || !jpeg || !len) return RSDK_E_PARAM;
    sqlite3 *db = meta; sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,"SELECT meta_off FROM meta_doc WHERE id=? AND doc_type=?",-1,&st,NULL)!=SQLITE_OK)
        return RSDK_E_DB;
    sqlite3_bind_int64(st,1,(sqlite3_int64)pic_id); sqlite3_bind_int(st,2,RSDK_DOC_SNAP);
    rsdk_err_t rc = RSDK_E_NOTFOUND;
    if (sqlite3_step(st) == SQLITE_ROW) rc = read_blob(d, (uint64_t)sqlite3_column_int64(st,0), jpeg, len);
    sqlite3_finalize(st);
    return rc;
}

int rsdk_pic_list_event(void *meta, uint64_t event_id, int type, rsdk_pic_ref_t *out, int cap) {
    if (!meta || !out || cap <= 0) return 0;
    sqlite3 *db = meta; sqlite3_stmt *st;
    const char *S = type < 0
        ? "SELECT id,ts,chn,meta_disk,meta_off,meta_len,json_extract(json,'$.pic') "
          "FROM meta_doc WHERE event_id=? AND doc_type=? ORDER BY id"
        : "SELECT id,ts,chn,meta_disk,meta_off,meta_len,json_extract(json,'$.pic') "
          "FROM meta_doc WHERE event_id=? AND doc_type=? AND json_extract(json,'$.pic')=? ORDER BY id";
    if (sqlite3_prepare_v2(db, S, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st,1,(sqlite3_int64)event_id); sqlite3_bind_int(st,2,RSDK_DOC_SNAP);
    if (type >= 0) sqlite3_bind_int(st,3,type);
    int n = 0;
    while (n < cap && sqlite3_step(st) == SQLITE_ROW) {
        rsdk_pic_ref_t *r = &out[n++];
        r->pic_id=(uint64_t)sqlite3_column_int64(st,0); r->ts=(uint32_t)sqlite3_column_int64(st,1);
        r->chn=sqlite3_column_int(st,2); r->disk=(uint16_t)sqlite3_column_int(st,3);
        r->off=(uint64_t)sqlite3_column_int64(st,4); r->len=(uint32_t)sqlite3_column_int(st,5);
        r->type=sqlite3_column_int(st,6);
    }
    sqlite3_finalize(st);
    return n;
}

rsdk_err_t rsdk_pic_get_for_event(rsdk_dev_t *d, void *meta, uint64_t event_id,
                                  int type, void **jpeg, size_t *len) {
    rsdk_pic_ref_t r[8];
    int want = (type < 0) ? RSDK_PIC_MAIN : type;
    int n = rsdk_pic_list_event(meta, event_id, want, r, 8);
    if (n <= 0 && type < 0) n = rsdk_pic_list_event(meta, event_id, -1, r, 8); /* 无主图则取任意 */
    if (n <= 0) return RSDK_E_NOTFOUND;
    return read_blob(d, r[0].off, jpeg, len);
}
