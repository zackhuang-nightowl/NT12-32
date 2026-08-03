/* Copyright (C) 2025-2026, Nightowl DG. RSDK 多盘负载均衡 + 多盘回放(设计 §4/§7.4). */
#include "rsdk_balance.h"
#include "rsdk_index.h"
#include "rsdk_play.h"
#include <stdlib.h>
#include <string.h>

struct rsdk_group { rsdk_dev_t **devs; int n; unsigned rr; };

/* 盘负载(0..1): 已分配 chunk / 数据 chunk 容量 */
static double dev_load(rsdk_dev_t *d) {
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    uint64_t data = sb->chunk_count - sb->meta_chunk_count;
    return data ? (double)sb->write_ptr_chunk / (double)data : 1.0;
}

rsdk_err_t rsdk_group_open(const char *const *paths, int n, rsdk_group_t **out) {
    if (!paths || n <= 0 || !out) return RSDK_E_PARAM;
    rsdk_group_t *g = calloc(1, sizeof *g);
    if (!g) return RSDK_E_IO;
    g->devs = calloc(n, sizeof(rsdk_dev_t*));
    for (int i = 0; i < n; i++) {
        rsdk_err_t rc = rsdk_dev_open(paths[i], &g->devs[i]);
        if (rc) { for (int k=0;k<i;k++) rsdk_dev_close(g->devs[k]); free(g->devs); free(g); return rc; }
    }
    g->n = n; *out = g;
    return RSDK_OK;
}

int         rsdk_group_count(rsdk_group_t *g)        { return g ? g->n : 0; }
rsdk_dev_t *rsdk_group_dev(rsdk_group_t *g, int i)   { return (g && i>=0 && i<g->n) ? g->devs[i] : NULL; }

/* 选盘(设计 §4.2): 取负载最低的盘; 多盘并列时用轮转计数打散 → 一路录像均摊到多盘。
 * (通道亲和/写带宽/覆盖压力可在此扩展权重; 当前以剩余空间为主) */
rsdk_err_t rsdk_balance_pick(rsdk_group_t *g, int chn, rsdk_dev_t **picked) {
    (void)chn;
    if (!g || !picked) return RSDK_E_PARAM;
    double best = 2.0;
    for (int i = 0; i < g->n; i++) { double l = dev_load(g->devs[i]); if (l < best) best = l; }
    const double EPS = 1.0 / 100000.0;           /* 并列判定阈 */
    int cand[64], nc = 0;
    for (int i = 0; i < g->n && nc < 64; i++)
        if (dev_load(g->devs[i]) <= best + EPS) cand[nc++] = i;
    int pick = cand[g->rr++ % nc];               /* 并列盘间轮转 */
    *picked = g->devs[pick];
    return RSDK_OK;
}

void rsdk_group_close(rsdk_group_t *g) {
    if (!g) return;
    for (int i = 0; i < g->n; i++) rsdk_dev_close(g->devs[i]);
    free(g->devs); free(g);
}

/* ==================== 多盘回放 ==================== */

int rsdk_group_query(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                     int rectype, rsdk_index_slot_t *out, int cap) {
    if (!g || !out || cap <= 0) return 0;
    int total = 0;
    for (int i = 0; i < g->n && total < cap; i++) {
        int got = rsdk_index_query(g->devs[i], t0, t1, chn, rectype, out + total, cap - total);
        for (int k = 0; k < got; k++) out[total + k].start_disk = (uint16_t)i; /* 重写为数组下标 */
        total += got;
    }
    /* 归并按 start_time 升序(跨盘) */
    for (int a = 0; a < total; a++) for (int b = a + 1; b < total; b++)
        if (out[b].start_time < out[a].start_time) { rsdk_index_slot_t t = out[a]; out[a] = out[b]; out[b] = t; }
    return total;
}

struct rsdk_group_player {
    rsdk_group_t *g;
    rsdk_index_slot_t *segs; int nseg, cur;
    rsdk_player_t *pl;          /* 当前段的单盘回放器 */
    uint64_t seek_pts; int seek_pending;
};

static rsdk_err_t open_cur(rsdk_group_player_t *p) {
    if (p->pl) { rsdk_play_close(p->pl); p->pl = NULL; }
    if (p->cur >= p->nseg) return RSDK_E_NOTFOUND;
    rsdk_index_slot_t *s = &p->segs[p->cur];
    rsdk_dev_t *d = rsdk_group_dev(p->g, s->start_disk);
    if (!d) return RSDK_E_NOTFOUND;
    rsdk_err_t rc = rsdk_play_open(d, s, &p->pl);
    if (rc) return rc;
    if (p->seek_pending) { rsdk_play_seek_pts(p->pl, p->seek_pts); p->seek_pending = 0; }
    return RSDK_OK;
}

rsdk_err_t rsdk_group_play_open(rsdk_group_t *g, const rsdk_index_slot_t *segs, int nseg,
                                rsdk_group_player_t **out) {
    if (!g || !segs || nseg <= 0 || !out) return RSDK_E_PARAM;
    rsdk_group_player_t *p = calloc(1, sizeof *p);
    if (!p) return RSDK_E_IO;
    p->g = g; p->nseg = nseg; p->cur = 0;
    p->segs = malloc((size_t)nseg * sizeof *segs);
    memcpy(p->segs, segs, (size_t)nseg * sizeof *segs);
    rsdk_err_t rc = open_cur(p);
    if (rc) { free(p->segs); free(p); return rc; }
    *out = p;
    return RSDK_OK;
}

rsdk_err_t rsdk_group_play_next(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                const uint8_t **data, uint32_t *len, int *disk_out) {
    if (!p || !hdr || !data || !len) return RSDK_E_PARAM;
    for (;;) {
        if (!p->pl) return RSDK_E_NOTFOUND;
        rsdk_err_t rc = rsdk_play_next_frame(p->pl, hdr, data, len);
        if (rc == RSDK_OK) { if (disk_out) *disk_out = p->segs[p->cur].start_disk; return RSDK_OK; }
        /* 当前段播完 → 下一段(可能在别的盘) */
        p->cur++;
        if (open_cur(p) != RSDK_OK) return RSDK_E_NOTFOUND;
    }
}

/* 定位到全局 pts: 跳到覆盖该 pts 的段并在段内 seek */
rsdk_err_t rsdk_group_play_seek_pts(rsdk_group_player_t *p, uint64_t pts) {
    if (!p) return RSDK_E_PARAM;
    int idx = 0;
    for (int i = 0; i < p->nseg; i++) {
        rsdk_frame_hdr_t h0; const uint8_t *d; uint32_t l;
        rsdk_player_t *tmp; rsdk_dev_t *dev = rsdk_group_dev(p->g, p->segs[i].start_disk);
        if (rsdk_play_open(dev, &p->segs[i], &tmp) != RSDK_OK) continue;
        rsdk_err_t rc = rsdk_play_next_frame(tmp, &h0, &d, &l);
        rsdk_play_close(tmp);
        if (rc == RSDK_OK && h0.pts <= pts) idx = i; else if (rc == RSDK_OK) break;
    }
    p->cur = idx; p->seek_pts = pts; p->seek_pending = 1;
    return open_cur(p);
}

void rsdk_group_play_close(rsdk_group_player_t *p) {
    if (!p) return;
    if (p->pl) rsdk_play_close(p->pl);
    free(p->segs); free(p);
}
