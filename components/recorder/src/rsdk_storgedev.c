/* Copyright (C) 2025-2026, Nightowl DG. RSDK 盘注册/格式化/超级块(设计 §2, 冻结 §1). */
#define _GNU_SOURCE
#include "rsdk_storgedev.h"
#include "rsdk_crypto.h"
#include "rsdk_feature.h"
#include "rsdk_util.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct rsdk_dev {
    rsdk_rawdev_t     *raw;
    rsdk_superblock_t  sb;
    rsdk_systab_t      st;
    rsdk_crypto_t     *crypto;      /* NULL=明文 */
    uint64_t           chunk_bytes;
    uint64_t           data_chunks; /* 数据环范围(不含 meta 配额) */
    uint64_t           meta_base;   /* MetaRegion 起始绝对字节偏移 */
    uint8_t *badmap;         /* 坏 chunk 位图: 1 bit/数据 chunk; NULL 若 data_chunks==0 */
    uint64_t badmap_bytes;   /* = (data_chunks+7)/8 */
    uint64_t bad_bak_off;    /* 备份副本盘上绝对字节偏移; 0=无备份 */
};

#define SB_MAIN_SEC   1u
#define SB_BAK_SEC    2u
#define ST_MAIN_SEC   3u   /* SysTab 8 扇区 */
#define ST_BAK_SEC    11u
#define BITMAP_SEC    19u

int rsdk_sector_supported(uint32_t logical_sec) { return logical_sec == 512 ? 1 : 0; }

static void rnd_fill(uint8_t *p, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { ssize_t r = read(fd, p, n); (void)r; close(fd); }
    else for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(0x11 * (i + 1));
}

static uint32_t pick_chunk_mib(uint64_t total_bytes, uint32_t req) {
    if (req) return req;
    uint64_t tb = total_bytes;
    if (tb <= (4ULL<<40)) return 8;
    if (tb <= (16ULL<<40)) return 16;
    return 32;
}

/* §1.1 自动布局: 迭代求 chunk_count / rsv / data_start */
static void compute_layout(uint64_t total_sectors, uint32_t chunk_mib, uint32_t slots,
                           uint64_t *chunk_sectors, uint64_t *chunk_count,
                           uint64_t *data_start_sec, uint64_t *bitmap_sectors,
                           uint64_t *index_sectors) {
    uint64_t total = total_sectors * RSDK_SEC;
    uint64_t chunk = (uint64_t)chunk_mib << 20;
    uint64_t cc = total / chunk;
    uint64_t bmS = 0, ixS = 0, ds = 0;
    for (int it = 0; it < 4; it++) {
        uint64_t bitmap_bytes = ((cc * 2 + 7) / 8) * 2;              /* 2bit/chunk, 主备 */
        uint64_t index_bytes  = cc * slots * (uint64_t)sizeof(rsdk_index_slot_t); /* 单副本实用 */
        bmS = rsdk_align_up(bitmap_bytes, RSDK_SEC) / RSDK_SEC;
        ixS = rsdk_align_up(index_bytes,  RSDK_SEC) / RSDK_SEC;
        uint64_t rsv_end = (BITMAP_SEC + bmS + ixS) * RSDK_SEC;
        ds = rsdk_align_up(rsv_end, chunk) / RSDK_SEC;
        cc = (total - ds * RSDK_SEC) / chunk;
    }
    *chunk_sectors = chunk / RSDK_SEC;
    *chunk_count = cc; *data_start_sec = ds;
    *bitmap_sectors = bmS; *index_sectors = ixS;
}

rsdk_err_t rsdk_plan_layout(uint64_t total_sectors, uint32_t chunk_mib_req,
                            uint32_t slots, uint32_t feature_mask,
                            double meta_ratio_pct, rsdk_layout_t *out) {
    if (!out) return RSDK_E_PARAM;
    if (total_sectors < 64ULL * 2048) return RSDK_E_NOSPACE;   /* <64MiB */
    if (slots == 0) slots = RSDK_CFG_SLOTS_PER_CHUNK;
    uint32_t cmib = pick_chunk_mib(total_sectors * RSDK_SEC, chunk_mib_req);
    double mratio = (meta_ratio_pct > 0) ? meta_ratio_pct
                                         : ((double)RSDK_CFG_META_RATIO_PCT10 / 10.0);
    uint64_t cs, cc, ds, bmS, ixS;
    compute_layout(total_sectors, cmib, slots, &cs, &cc, &ds, &bmS, &ixS);
    if (cc == 0) return RSDK_E_NOSPACE;
    uint64_t meta_cc = (feature_mask & RSDK_FEAT_METADATA) ? (uint64_t)(cc * mratio / 100.0) : 0;
    if ((feature_mask & RSDK_FEAT_METADATA) && meta_cc == 0 && cc > 4) meta_cc = 1;
    if (meta_cc >= cc) return RSDK_E_NOSPACE;
    out->chunk_mib = cmib;
    out->chunk_sectors = cs;
    out->chunk_count = cc;
    out->data_start_sec = ds;
    out->bitmap_sectors = bmS;
    out->index_sectors = ixS;
    out->meta_chunk_count = meta_cc;
    out->data_chunk_count = cc - meta_cc;
    out->index_slot_count = (uint32_t)(ixS * RSDK_SEC / sizeof(rsdk_index_slot_t));
    if (out->data_chunk_count == 0) return RSDK_E_NOSPACE;
    return RSDK_OK;
}

rsdk_err_t rsdk_peek_superblock(const char *devpath, rsdk_superblock_t *sb) {
    if (!devpath || !sb) return RSDK_E_PARAM;
    rsdk_rawdev_t *raw;
    rsdk_err_t rc = rsdk_rawdev_open(devpath, &raw);
    if (rc) return rc;
    rc = rsdk_rawdev_pread(raw, SB_MAIN_SEC * RSDK_SEC, sb, sizeof *sb);
    rsdk_rawdev_close(raw);
    if (rc) return RSDK_E_IO;
    if (memcmp(sb->magic, RSDK_SB_MAGIC, 8) != 0) return RSDK_E_FORMAT;
    rsdk_superblock_t t = *sb; t.sb_crc32 = 0;
    if (rsdk_crc32(&t, sizeof t) != sb->sb_crc32) return RSDK_E_FORMAT;
    return RSDK_OK;
}

rsdk_err_t rsdk_format(const char *path, const rsdk_format_opt_t *opt)
{
    rsdk_rawdev_t *raw;
    rsdk_err_t rc = rsdk_rawdev_open(path, &raw);
    if (rc) return rc;
    uint64_t sectors = rsdk_rawdev_sectors(raw);
    if (sectors < 64ULL * 2048) { rsdk_rawdev_close(raw); return RSDK_E_NOSPACE; } /* <64MB */
    if (!rsdk_sector_supported(rsdk_rawdev_logical_sec(raw))) {
        rsdk_rawdev_close(raw); return RSDK_E_SECTORSIZE;   /* 4Kn 等非512逻辑扇区: 干净拒绝 */
    }

    uint32_t fmask = (opt && opt->feature_mask) ? opt->feature_mask : rsdk_feature_mask();
    uint32_t slots = (opt && opt->slots_per_chunk) ? opt->slots_per_chunk : RSDK_CFG_SLOTS_PER_CHUNK;
    uint32_t cmib_req = opt ? opt->chunk_mib : 0;
    double   mratio   = (opt && opt->meta_ratio_pct > 0) ? opt->meta_ratio_pct : 0.0;

    rsdk_layout_t L;
    rsdk_err_t lrc = rsdk_plan_layout(sectors, cmib_req, slots, fmask, mratio, &L);
    if (lrc != RSDK_OK) { rsdk_rawdev_close(raw); return lrc; }

    /* ---- SuperBlock ---- */
    rsdk_superblock_t sb; memset(&sb, 0, sizeof sb);
    memcpy(sb.magic, RSDK_SB_MAGIC, 8);
    sb.version = RSDK_FORMAT_VERSION;
    rnd_fill(sb.disk_uuid, 16);
    if (opt && opt->sn) { memset(sb.group_uuid, 0, 16); memcpy(sb.group_uuid, "grp1", 4); }
    else memcpy(sb.group_uuid, "grp1", 4);
    sb.group_disk_index = 0; sb.group_disk_count = 1;
    sb.total_sectors = sectors; sb.rsv_start_sec = 1; sb.data_start_sec = L.data_start_sec;
    sb.chunk_sectors = L.chunk_sectors; sb.chunk_count = L.chunk_count;
    sb.write_ptr_chunk = 0; sb.seq_epoch = 1;
    sb.feature_mask = fmask;
    sb.hdd_full = opt ? opt->hdd_full : (uint8_t)RSDK_CFG_HDD_FULL;
    sb.meta_start_chunk = L.data_chunk_count; sb.meta_chunk_count = L.meta_chunk_count;

    if (fmask & RSDK_FEAT_ENCRYPTION) {
        sb.enc_algo = 1;
        sb._rsv0[0] = 1;           /* kdf_id=1: PBKDF2-HMAC-SHA256 */
        rnd_fill(sb.kdf_salt, 16);
        uint8_t dek[32], kek[32];
        rnd_fill(dek, 32);
        rsdk_kdf_kek2(opt && opt->sn ? opt->sn : RSDK_DEFAULT_SN, sb.kdf_salt, 1, kek);
        rsdk_dek_wrap(kek, dek, sb.wrapped_dek, sb.dek_kcv);
        memset(dek, 0, 32);
    }
    sb.sb_crc32 = 0;
    sb.sb_crc32 = rsdk_crc32(&sb, sizeof sb);

    /* ---- SysTab ---- */
    rsdk_systab_t st; memset(&st, 0, sizeof st);
    st.magic = 0x42415453u; /* 'STAB' */
    st.sys_version = 1; st.systab_size = sizeof st;
    st.format_time = opt ? opt->format_time : 0;
    st.bitmap_start_sec = BITMAP_SEC; st.bitmap_sectors = L.bitmap_sectors;
    st.index_start_sec = BITMAP_SEC + L.bitmap_sectors; st.index_sectors = L.index_sectors;
    st.index_slot_size = sizeof(rsdk_index_slot_t);
    st.index_slot_count = L.index_slot_count;
    st.index_next = 0;
    st.meta_next_off = 0;
    st.meta_bytes = L.meta_chunk_count * (L.chunk_sectors * RSDK_SEC);  /* MetaRegion 字节大小 */
    st.crc32 = 0; st.crc32 = rsdk_crc32(&st, sizeof st);

    /* ---- 写盘: 保护扇区0清零 + SB主备 + SysTab主备 + 位图清零 + 索引区清零 ---- */
    uint8_t zero[RSDK_SEC]; memset(zero, 0, sizeof zero);
    rsdk_rawdev_pwrite(raw, 0, zero, RSDK_SEC);
    rsdk_rawdev_pwrite(raw, SB_MAIN_SEC*RSDK_SEC, &sb, sizeof sb);
    rsdk_rawdev_pwrite(raw, SB_BAK_SEC*RSDK_SEC,  &sb, sizeof sb);
    rsdk_rawdev_pwrite(raw, ST_MAIN_SEC*RSDK_SEC, &st, sizeof st);
    rsdk_rawdev_pwrite(raw, ST_BAK_SEC*RSDK_SEC,  &st, sizeof st);
    /* 清位图 + 索引区(小块循环写0) */
    uint8_t blk[RSDK_SEC]; memset(blk, 0, sizeof blk);
    for (uint64_t s = BITMAP_SEC; s < st.index_start_sec + L.index_sectors; s++)
        rsdk_rawdev_pwrite(raw, s*RSDK_SEC, blk, RSDK_SEC);
    rsdk_rawdev_sync(raw);
    rsdk_rawdev_close(raw);
    return RSDK_OK;
}

/* 读一处 [crc32|bits] 副本, crc 通过则拷入 dst 返回1 */
static int badmap_read_copy(rsdk_dev_t *d, uint64_t abs_off, uint8_t *dst, uint64_t n) {
    uint32_t crc = 0;
    if (rsdk_rawdev_pread(d->raw, abs_off, &crc, 4)) return 0;
    if (rsdk_rawdev_pread(d->raw, abs_off + 4, dst, n)) return 0;
    return rsdk_crc32(dst, n) == crc;
}
static void badmap_write_copy(rsdk_dev_t *d, uint64_t abs_off, const uint8_t *src, uint64_t n) {
    uint32_t crc = rsdk_crc32(src, n);
    rsdk_rawdev_pwrite(d->raw, abs_off, &crc, 4);
    rsdk_rawdev_pwrite(d->raw, abs_off + 4, src, n);
}
static void badmap_save(rsdk_dev_t *d) {
    if (!d->badmap || !d->badmap_bytes) return;
    uint64_t region = d->st.bitmap_start_sec * RSDK_SEC;
    badmap_write_copy(d, region, d->badmap, d->badmap_bytes);
    if (d->bad_bak_off) badmap_write_copy(d, d->bad_bak_off, d->badmap, d->badmap_bytes);
    rsdk_rawdev_sync(d->raw);
}

rsdk_err_t rsdk_dev_open(const char *path, rsdk_dev_t **out)
{
    rsdk_rawdev_t *raw;
    rsdk_err_t rc = rsdk_rawdev_open(path, &raw);
    if (rc) return rc;
    rsdk_dev_t *d = calloc(1, sizeof *d);
    if (!d) { rsdk_rawdev_close(raw); return RSDK_E_IO; }
    d->raw = raw;
    rsdk_rawdev_pread(raw, SB_MAIN_SEC*RSDK_SEC, &d->sb, sizeof d->sb);
    if (memcmp(d->sb.magic, RSDK_SB_MAGIC, 8) != 0) {  /* 主坏取备 */
        rsdk_rawdev_pread(raw, SB_BAK_SEC*RSDK_SEC, &d->sb, sizeof d->sb);
        if (memcmp(d->sb.magic, RSDK_SB_MAGIC, 8) != 0) { rsdk_dev_close(d); return RSDK_E_FORMAT; }
    }
    { rsdk_superblock_t t = d->sb; t.sb_crc32 = 0;
      if (rsdk_crc32(&t, sizeof t) != d->sb.sb_crc32) { rsdk_dev_close(d); return RSDK_E_CORRUPT; } }
    rsdk_rawdev_pread(raw, ST_MAIN_SEC*RSDK_SEC, &d->st, sizeof d->st);
    if (d->st.magic != 0x42415453u)
        rsdk_rawdev_pread(raw, ST_BAK_SEC*RSDK_SEC, &d->st, sizeof d->st);
    d->chunk_bytes = d->sb.chunk_sectors * RSDK_SEC;
    d->data_chunks = d->sb.chunk_count - d->sb.meta_chunk_count;
    d->meta_base = d->sb.data_start_sec * RSDK_SEC + d->sb.meta_start_chunk * d->chunk_bytes;

    /* 坏 chunk 位图: 从 bitmap 区加载(主/备+CRC), 无效→视为无坏块(旧盘全零) */
    d->badmap_bytes = (d->data_chunks + 7) / 8;
    if (d->badmap_bytes) {
        uint64_t region = d->st.bitmap_start_sec * RSDK_SEC;
        uint64_t half   = (uint64_t)(d->st.bitmap_sectors / 2) * RSDK_SEC;
        d->bad_bak_off  = (half >= 4 + d->badmap_bytes) ? region + half : 0;
        d->badmap = calloc(1, d->badmap_bytes);
        uint8_t *tmp = d->badmap ? malloc(d->badmap_bytes) : NULL;
        if (d->badmap && tmp) {
            if (badmap_read_copy(d, region, tmp, d->badmap_bytes))
                memcpy(d->badmap, tmp, d->badmap_bytes);
            else if (d->bad_bak_off && badmap_read_copy(d, d->bad_bak_off, tmp, d->badmap_bytes))
                memcpy(d->badmap, tmp, d->badmap_bytes);
            /* 两副本都无效 → 保持 calloc 的全零(无坏块) */
        }
        free(tmp);
    }

    if (d->sb.enc_algo == 1) {  /* 装配加密: kdf_id → KEK, 解包 DEK */
        uint8_t kek[32], dek[32];
        uint32_t kid = d->sb._rsv0[0];   /* 旧盘 kid=0→legacy; 新盘 kid=1→PBKDF2 */
        rsdk_kdf_kek2(RSDK_DEFAULT_SN, d->sb.kdf_salt, kid, kek);
        if (rsdk_dek_unwrap(kek, d->sb.wrapped_dek, d->sb.dek_kcv, dek) == RSDK_OK)
            rsdk_crypto_open(dek, RSDK_CFG_ENCRYPTION, &d->crypto);
        memset(dek, 0, 32);
    }
    *out = d;
    return RSDK_OK;
}

rsdk_err_t rsdk_dev_info(rsdk_dev_t *d, rsdk_dev_info_t *info)
{
    if (!d || !info) return RSDK_E_PARAM;
    info->total_sectors = d->sb.total_sectors;
    info->chunk_count = d->sb.chunk_count;
    info->chunk_sectors = d->sb.chunk_sectors;
    info->data_start_sec = d->sb.data_start_sec;
    info->meta_chunk_count = d->sb.meta_chunk_count;
    info->free_chunks = d->data_chunks; /* 简化: 环形, 视为始终可写 */
    info->feature_mask = d->sb.feature_mask;
    info->enc_algo = d->sb.enc_algo;
    info->hdd_full = d->sb.hdd_full;
    return RSDK_OK;
}

rsdk_rawdev_t     *rsdk_dev_raw(rsdk_dev_t *d)    { return d->raw; }
rsdk_superblock_t *rsdk_dev_sb (rsdk_dev_t *d)    { return &d->sb; }
rsdk_systab_t     *rsdk_dev_systab(rsdk_dev_t *d) { return &d->st; }
struct rsdk_crypto *rsdk_dev_crypto(rsdk_dev_t *d){ return d->crypto; }

rsdk_err_t rsdk_dev_flush(rsdk_dev_t *d)
{
    d->sb.sb_crc32 = 0; d->sb.sb_crc32 = rsdk_crc32(&d->sb, sizeof d->sb);
    d->st.crc32 = 0; d->st.crc32 = rsdk_crc32(&d->st, sizeof d->st);
    rsdk_rawdev_pwrite(d->raw, SB_BAK_SEC*RSDK_SEC, &d->sb, sizeof d->sb); /* 先备后主 */
    rsdk_rawdev_pwrite(d->raw, ST_BAK_SEC*RSDK_SEC, &d->st, sizeof d->st);
    rsdk_rawdev_sync(d->raw);
    rsdk_rawdev_pwrite(d->raw, SB_MAIN_SEC*RSDK_SEC, &d->sb, sizeof d->sb);
    rsdk_rawdev_pwrite(d->raw, ST_MAIN_SEC*RSDK_SEC, &d->st, sizeof d->st);
    return rsdk_rawdev_sync(d->raw);
}

rsdk_err_t rsdk_dev_mark_bad_chunk(rsdk_dev_t *d, uint64_t chunk) {
    if (!d || chunk >= d->data_chunks) return RSDK_E_PARAM;
    if (!d->badmap) return RSDK_E_NOSPACE;
    if (!((d->badmap[chunk >> 3] >> (chunk & 7)) & 1)) {
        d->badmap[chunk >> 3] |= (uint8_t)(1u << (chunk & 7));
        badmap_save(d);
    }
    return RSDK_OK;
}
int rsdk_dev_is_bad_chunk(rsdk_dev_t *d, uint64_t chunk) {
    if (!d || !d->badmap || chunk >= d->data_chunks) return 0;
    return (d->badmap[chunk >> 3] >> (chunk & 7)) & 1;
}
uint64_t rsdk_dev_bad_chunk_count(rsdk_dev_t *d) {
    if (!d || !d->badmap) return 0;
    uint64_t n = 0;
    for (uint64_t c = 0; c < d->data_chunks; c++) n += (d->badmap[c >> 3] >> (c & 7)) & 1;
    return n;
}

rsdk_err_t rsdk_dev_alloc_chunk(rsdk_dev_t *d, uint64_t *chunk, uint64_t *byte_off)
{
    if (d->data_chunks == 0) return RSDK_E_NOSPACE;
    for (uint64_t tried = 0; tried < d->data_chunks; tried++) {
        /* stop 策略: 写满即停 */
        if (d->sb.hdd_full == RSDK_HDDFULL_STOP && d->sb.write_ptr_chunk >= d->data_chunks)
            return RSDK_E_NOSPACE;
        uint64_t c = d->sb.write_ptr_chunk;
        d->sb.write_ptr_chunk++;
        if (d->sb.write_ptr_chunk >= d->data_chunks) {
            if (d->sb.hdd_full == RSDK_HDDFULL_STOP) { /* 停在末尾, 下次 NOSPACE */ }
            else { d->sb.write_ptr_chunk = 0; d->sb.seq_epoch++; }  /* overwrite: 回绕 */
        }
        if (rsdk_dev_is_bad_chunk(d, c)) continue;            /* 跳坏 chunk */
        if (chunk) *chunk = c;
        if (byte_off) *byte_off = d->sb.data_start_sec * RSDK_SEC + c * d->chunk_bytes;
        return RSDK_OK;
    }
    return RSDK_E_NOSPACE;   /* 一整圈都坏 */
}

int rsdk_dev_is_wrapped(rsdk_dev_t *d) { return d && d->sb.seq_epoch > 1; }

rsdk_err_t rsdk_dev_meta_alloc(rsdk_dev_t *d, uint64_t size, uint64_t *abs_off)
{
    if (!d || !abs_off) return RSDK_E_PARAM;
    if (d->st.meta_bytes == 0) return RSDK_E_NOSPACE;      /* metadata=off, 无 MetaRegion */
    size = rsdk_align_up(size, 16);                        /* 16 对齐(CTR 友好) */
    if (size > d->st.meta_bytes) return RSDK_E_NOSPACE;
    if (d->st.meta_next_off + size > d->st.meta_bytes) d->st.meta_next_off = 0; /* 环形回绕 */
    *abs_off = d->meta_base + d->st.meta_next_off;
    d->st.meta_next_off += size;
    rsdk_dev_flush(d);
    return RSDK_OK;
}

uint16_t rsdk_dev_index(rsdk_dev_t *d) { return d ? d->sb.group_disk_index : 0; }

rsdk_err_t rsdk_dev_rekey(rsdk_dev_t *d)
{
    if (!d || d->sb.enc_algo != 1) return RSDK_E_PARAM;
    uint32_t kid = d->sb._rsv0[0];     /* 保持盘的现有 kdf_id, 不静默升级 */
    uint8_t kek[32], dek[32];
    rsdk_kdf_kek2(RSDK_DEFAULT_SN, d->sb.kdf_salt, kid, kek);
    if (rsdk_dek_unwrap(kek, d->sb.wrapped_dek, d->sb.dek_kcv, dek) != RSDK_OK) return RSDK_E_CRYPTO;
    uint8_t new_salt[16], kek2[32];
    rnd_fill(new_salt, 16);
    rsdk_kdf_kek2(RSDK_DEFAULT_SN, new_salt, kid, kek2);  /* 新盐+同 kdf_id → 新 KEK */
    rsdk_dek_wrap(kek2, dek, d->sb.wrapped_dek, d->sb.dek_kcv);   /* 重封装同一 DEK */
    memcpy(d->sb.kdf_salt, new_salt, 16);
    memset(dek, 0, 32); memset(kek, 0, 32); memset(kek2, 0, 32);
    return rsdk_dev_flush(d);                             /* DEK 不变 → 已加密数据仍可解 */
}

void rsdk_dev_close(rsdk_dev_t *d)
{
    if (!d) return;
    if (d->crypto) rsdk_crypto_close(d->crypto);
    if (d->raw) rsdk_rawdev_close(d->raw);
    free(d->badmap);
    free(d);
}
