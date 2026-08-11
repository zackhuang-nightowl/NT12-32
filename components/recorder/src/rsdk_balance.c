/* Copyright (C) 2025-2026, Nightowl DG. RSDK 多盘负载均衡 + 多盘回放(设计 §4/§7.4). */
#include "rsdk_balance.h"
#include "rsdk_disk.h"
#include "rsdk_index.h"
#include "rsdk_play.h"
#include <stdlib.h>
#include <string.h>

/* ==================== 盘组结构 ==================== */

struct rsdk_group {
    rsdk_dev_t **devs;
    int          n;
    unsigned     rr;
    char       **paths;      /* strdup 的 devpath, 供 SMART 刷新 */
    int         *health_ok;  /* 每盘健康标志: 1=健康/未知放行, 0=病盘 */
    double      *bw;         /* 每盘 EWMA 写带宽(字节/段, 近似) */
    int          chn_last[32]; /* 每通道上次落盘下标, -1=未分配 */
};

/* 盘负载(0..1): 已写 chunk / 数据 chunk 总量 */
static double dev_load(rsdk_dev_t *d) {
    rsdk_superblock_t *sb = rsdk_dev_sb(d);
    uint64_t data = sb->chunk_count - sb->meta_chunk_count;
    return data ? (double)sb->write_ptr_chunk / (double)data : 1.0;
}

/* ==================== 盘组开/关 ==================== */

rsdk_err_t rsdk_group_open(const char *const *paths, int n, rsdk_group_t **out) {
    if (!paths || n <= 0 || !out) return RSDK_E_PARAM;
    rsdk_group_t *g = calloc(1, sizeof *g);
    if (!g) return RSDK_E_IO;

    g->devs      = calloc(n, sizeof(rsdk_dev_t *));
    g->paths     = calloc(n, sizeof(char *));
    g->health_ok = calloc(n, sizeof(int));
    g->bw        = calloc(n, sizeof(double));
    if (!g->devs || !g->paths || !g->health_ok || !g->bw) {
        free(g->devs); free(g->paths); free(g->health_ok); free(g->bw); free(g);
        return RSDK_E_IO;
    }

    for (int i = 0; i < n; i++) {
        rsdk_err_t rc = rsdk_dev_open(paths[i], &g->devs[i]);
        if (rc) {
            for (int k = 0; k < i; k++) { rsdk_dev_close(g->devs[k]); free(g->paths[k]); }
            free(g->devs); free(g->paths); free(g->health_ok); free(g->bw); free(g);
            return rc;
        }
        g->paths[i]     = strdup(paths[i]);
        g->health_ok[i] = 1;   /* 初始: 健康/未知均放行 */
        g->bw[i]        = 0.0;
    }
    g->n = n;
    for (int i = 0; i < 32; i++) g->chn_last[i] = -1;
    *out = g;
    return RSDK_OK;
}

int         rsdk_group_count(rsdk_group_t *g)      { return g ? g->n : 0; }
rsdk_dev_t *rsdk_group_dev(rsdk_group_t *g, int i) { return (g && i>=0 && i<g->n) ? g->devs[i] : NULL; }

void rsdk_group_close(rsdk_group_t *g) {
    if (!g) return;
    for (int i = 0; i < g->n; i++) {
        rsdk_dev_close(g->devs[i]);
        free(g->paths[i]);
    }
    free(g->devs); free(g->paths); free(g->health_ok); free(g->bw);
    free(g);
}

/* ==================== 新 API ==================== */

/* SMART 健康刷新: 对每块盘读 SMART 并更新 health_ok[].
 * 固件按分钟级定时调用, 保持 SG_IO 离热选路径。 */
rsdk_err_t rsdk_group_smart_refresh(rsdk_group_t *g) {
    if (!g) return RSDK_E_PARAM;
    for (int i = 0; i < g->n; i++) {
        rsdk_smart_t s;
        rsdk_err_t rc = rsdk_smart_read(g->paths[i], &s);
        if (rc == RSDK_OK)
            g->health_ok[i] = rsdk_smart_ok(&s);
        /* rc != OK: 取不到 SMART → 保留现有状态 */
    }
    return RSDK_OK;
}

/* 报告段字节: 更新 EWMA 写带宽。由录像层在段结束时调用。
 * EWMA: bw[i] = bw[i]*0.7 + bytes*0.3 */
void rsdk_balance_report(rsdk_group_t *g, rsdk_dev_t *dev, uint64_t bytes) {
    if (!g || !dev) return;
    for (int i = 0; i < g->n; i++) {
        if (g->devs[i] == dev) {
            g->bw[i] = g->bw[i] * 0.7 + (double)bytes * 0.3;
            return;
        }
    }
}

/* 测试钩子: 强制设置健康状态(绕过 SMART, 用于 image 文件上的测试) */
void rsdk_group_set_health(rsdk_group_t *g, int disk, int ok) {
    if (!g || disk < 0 || disk >= g->n) return;
    g->health_ok[disk] = ok;
}

/* ==================== 选盘(设计 §4.2) ==================== */

/* wrapped: seq_epoch > 1 (已绕盘至少一圈, 存在覆盖压力) */
static int dev_wrapped(rsdk_dev_t *d) {
    return rsdk_dev_sb(d)->seq_epoch > 1;
}

/* 评分函数: 分数越低越优先写入
 *   fill_i     = dev_load(dev_i)                               0..1
 *   pressure_i = fill_i * (wrapped ? 1.0 : 0.5)               覆盖压力
 *   bw_norm_i  = bw[i] / (max_bw + 1.0)                       归一化带宽
 *   score_i    = 0.4*bw_norm_i + 0.4*fill_i + 0.2*pressure_i
 */
rsdk_err_t rsdk_balance_pick(rsdk_group_t *g, int chn, rsdk_dev_t **picked) {
    if (!g || !picked) return RSDK_E_PARAM;

    /* 1. 最大带宽(归一化分母) */
    double max_bw = 0.0;
    for (int i = 0; i < g->n; i++)
        if (g->bw[i] > max_bw) max_bw = g->bw[i];

    /* 2. 计算每盘分数 */
    double scores[64]; /* 最多 64 盘 */
    int    nc = g->n > 64 ? 64 : g->n;
    for (int i = 0; i < nc; i++) {
        double fill_i     = dev_load(g->devs[i]);
        double pressure_i = fill_i * (dev_wrapped(g->devs[i]) ? 1.0 : 0.5);
        double bw_norm_i  = g->bw[i] / (max_bw + 1.0);
        scores[i] = 0.4 * bw_norm_i + 0.4 * fill_i + 0.2 * pressure_i;
    }

    /* 3. 候选集: health_ok != 0; 全病则退化为全部盘(录像优先于健康门控) */
    int cand[64], cand_n = 0;
    for (int i = 0; i < nc; i++)
        if (g->health_ok[i]) cand[cand_n++] = i;
    if (cand_n == 0) {
        /* fallback: 全盘病态, 仍须录像 */
        for (int i = 0; i < nc; i++) cand[cand_n++] = i;
    }

    /* 4. 候选集中最低分 */
    double min_score = scores[cand[0]];
    for (int j = 1; j < cand_n; j++)
        if (scores[cand[j]] < min_score) min_score = scores[cand[j]];

    /* 5. 通道亲和: 若上次用的盘仍是候选且分数不超最低 0.15, 则保持 */
    int slot = chn & 31;
    int last = g->chn_last[slot];
    int pick = -1;
    if (last >= 0 && last < nc && g->health_ok[last]) {
        /* last 在候选集中且满足亲和阈 */
        if (scores[last] <= min_score + 0.15) pick = last;
    }

    if (pick < 0) {
        /* 6. argmin score over candidates; 并列用 rr 打散 */
        int tied[64], nt = 0;
        const double EPS = 1.0 / 100000.0;
        for (int j = 0; j < cand_n; j++)
            if (scores[cand[j]] <= min_score + EPS) tied[nt++] = cand[j];
        pick = tied[g->rr++ % nt];
    }

    g->chn_last[slot] = pick;
    *picked = g->devs[pick];
    return RSDK_OK;
}

/* ==================== 多盘回放 ==================== */

int rsdk_group_query_stream(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                            int rectype, int stream, rsdk_index_slot_t *out, int cap) {
    if (!g || !out || cap <= 0) return 0;
    int total = 0;
    for (int i = 0; i < g->n && total < cap; i++) {
        int got = rsdk_index_query_stream(g->devs[i], t0, t1, chn, rectype, stream,
                                          out + total, cap - total);
        for (int k = 0; k < got; k++) out[total + k].start_disk = (uint16_t)i; /* 重写为数组下标 */
        total += got;
    }
    /* 归并按 start_time 升序(跨盘) */
    for (int a = 0; a < total; a++) for (int b = a + 1; b < total; b++)
        if (out[b].start_time < out[a].start_time) { rsdk_index_slot_t t = out[a]; out[a] = out[b]; out[b] = t; }
    return total;
}

int rsdk_group_query(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                     int rectype, rsdk_index_slot_t *out, int cap) {
    return rsdk_group_query_stream(g, t0, t1, chn, rectype, -1, out, cap);
}

struct rsdk_group_player {
    rsdk_group_t *g;
    rsdk_index_slot_t *segs; int nseg, cur;
    rsdk_player_t *pl;          /* 当前段的单盘回放器 */
    uint64_t seek_pts; int seek_pending;
    int      have_prev;         /* 间隙待交付标志: 1 = 下一帧是跨段间隙后第一帧 */
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

/* 读取段的第一帧 pts; 成功返回 1, 失败返回 0 */
static int seg_first_pts(rsdk_group_t *g, const rsdk_index_slot_t *s, uint64_t *out) {
    rsdk_dev_t *dev = rsdk_group_dev(g, s->start_disk);
    rsdk_player_t *tmp;
    if (!dev || rsdk_play_open(dev, s, &tmp) != RSDK_OK) return 0;
    rsdk_frame_hdr_t h; const uint8_t *d; uint32_t l;
    int ok = (rsdk_play_next_frame(tmp, &h, &d, &l) == RSDK_OK);
    if (ok) *out = h.pts;
    rsdk_play_close(tmp);
    return ok;
}

/* 跨段间隙容差: 2 秒 */
#define GAP_TOL_SEC 2u

rsdk_err_t rsdk_group_play_next2(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                 const uint8_t **data, uint32_t *len,
                                 int *disk_out, int *gap_out) {
    if (!p || !hdr || !data || !len) return RSDK_E_PARAM;
    for (;;) {
        if (!p->pl) return RSDK_E_NOTFOUND;
        rsdk_err_t rc = rsdk_play_next_frame(p->pl, hdr, data, len);
        if (rc == RSDK_OK) {
            if (disk_out) *disk_out = p->segs[p->cur].start_disk;
            /* 消费待交付的间隙标志(仅第一帧触发一次) */
            if (gap_out) { *gap_out = p->have_prev; p->have_prev = 0; }
            return RSDK_OK;
        }
        /* 当前段播完 → 检测与下段的间隙, 再切换 */
        uint32_t old_end = p->segs[p->cur].end_time;
        p->cur++;
        if (open_cur(p) != RSDK_OK) return RSDK_E_NOTFOUND;
        /* 间隙检测: 新段 start_time 比上段 end_time 晚超过容差.
         * 上段未闭合(end_time=0xFFFFFFFF)→无可靠 end, 不据此判间隙(避免 +容差 溢出误报)。 */
        if (old_end == 0xFFFFFFFFu) {
            p->have_prev = 0;
        } else {
            uint32_t new_start = p->segs[p->cur].start_time;
            p->have_prev = (new_start > old_end + GAP_TOL_SEC) ? 1 : 0;
        }
    }
}

rsdk_err_t rsdk_group_play_next(rsdk_group_player_t *p, rsdk_frame_hdr_t *hdr,
                                const uint8_t **data, uint32_t *len, int *disk_out) {
    return rsdk_group_play_next2(p, hdr, data, len, disk_out, NULL);
}

/* 定位到全局 pts: 二分查找最后一个首帧 pts <= target 的段, O(log n) 次打开 */
rsdk_err_t rsdk_group_play_seek_pts(rsdk_group_player_t *p, uint64_t pts) {
    if (!p) return RSDK_E_PARAM;
    int lo = 0, hi = p->nseg - 1, idx = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint64_t fp = 0;
        if (seg_first_pts(p->g, &p->segs[mid], &fp) && fp <= pts) {
            idx = mid; lo = mid + 1;  /* mid 可行, 尝试更右 */
        } else {
            hi = mid - 1;             /* mid 首帧超过目标或读失败, 向左收 */
        }
    }
    p->cur = idx; p->seek_pts = pts; p->seek_pending = 1;
    p->have_prev = 0;  /* seek 后重置间隙上下文 */
    return open_cur(p);
}

void rsdk_group_play_close(rsdk_group_player_t *p) {
    if (!p) return;
    if (p->pl) rsdk_play_close(p->pl);
    free(p->segs); free(p);
}
