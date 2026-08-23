/* Copyright (C) 2025-2026, Nightowl DG. RSDK 抓拍(PIC).
 * 事件触发时刻那帧 JPEG → 加密写 MetaRegion(环形) → 截图指针回填事件索引槽(权威,可扫盘重建)。
 * meta.db 不参与:取图只走事件槽 snap_off。仅在 metadata=on 时编译(复用 MetaRegion)。 */
#include "rsdk_pic.h"
#include "rsdk_evtidx.h"    /* 截图指针回填事件槽 */
#include "rsdk_crypto.h"
#include "rsdk_util.h"
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

rsdk_err_t rsdk_pic_write(rsdk_dev_t *d, const rsdk_pic_key_t *k,
                          const void *jpeg, size_t len, uint64_t *pic_id)
{
    if (!d || !k || !jpeg || !len) return RSDK_E_PARAM;
    if (len > RSDK_PIC_MAX_BYTES) return RSDK_E_PARAM;   /* 单张上限(20MP,宏可调);超出拒绝防挤爆环 */

    /* 分配 MetaRegion 空间(头 + JPEG),按 512 对齐 → 头落扇区边界,供扫盘重建按扇区定位 PIC0。 */
    uint64_t total = rsdk_align_up(sizeof(rsdk_pic_hdr_t) + len, RSDK_SEC), abs_off;
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

    /* 截图指针回填事件索引槽(权威;供只读事件区取图 + 扫盘重建)。无对应事件槽则无害。 */
    rsdk_evtidx_patch_snap(d, k->event_id, rsdk_dev_index(d), abs_off, (uint32_t)total, k->ts);

    if (pic_id) *pic_id = abs_off;
    return RSDK_OK;
}

/* expect_event_id!=0 时校验 PIC0 头 event_id 匹配(环覆盖后 snap_off 可能已被新事件占用,
 * 不匹配当无图返回 NOTFOUND,绝不返回错图)。 */
static rsdk_err_t read_blob(rsdk_dev_t *d, uint64_t off, uint64_t expect_event_id,
                            void **jpeg, size_t *len) {
    rsdk_pic_hdr_t h;
    rsdk_rawdev_pread(rsdk_dev_raw(d), off, &h, sizeof h);
    if (memcmp(h.magic, "PIC0", 4) != 0) return RSDK_E_CORRUPT;
    rsdk_pic_hdr_t t = h; t.crc32 = 0;
    if (rsdk_crc32(&t, sizeof t) != h.crc32) return RSDK_E_CORRUPT;
    if (expect_event_id && h.event_id != expect_event_id) return RSDK_E_NOTFOUND;  /* 已被覆盖成别的事件 */
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

rsdk_err_t rsdk_pic_read_blob(rsdk_dev_t *d, uint64_t off, uint64_t expect_event_id,
                              void **jpeg, size_t *len) {
    if (!d || !jpeg || !len) return RSDK_E_PARAM;
    return read_blob(d, off, expect_event_id, jpeg, len);
}
