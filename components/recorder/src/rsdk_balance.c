/* Copyright (C) 2025-2026, Nightowl DG. RSDK 多盘负载均衡 + 多盘回放(设计 §4/§7.4). */
#include "rsdk_balance.h"
#include "rsdk_disk.h"
#include "rsdk_index.h"
#include "rsdk_play.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ==================== 盘组结构 ==================== */

struct rsdk_group {
    rsdk_dev_t **devs;
    int          n;
    unsigned     rr;
    char       **paths;      /* strdup 的 devpath, 供 SMART 刷新 */
    int         *health_ok;  /* 每盘健康标志: 1=健康/未知放行, 0=病盘 */
    double      *bw;         /* 每盘 EWMA 写带宽(字节/段, 近似) */
    int          chn_last[32]; /* 每通道上次落盘下标, -1=未分配 */

    /* ★ 盘组共享递归锁: 组内所有盘的元数据(sb/st/index/badmap)与 balance 字段(health/bw/chn_last/rr)
     * 都串行在这一把锁上。rsdk_group_open 后令每盘 dev->lk 指向它 → 多盘写者迁移/跨盘选盘时只有一把锁,
     * 无锁序/死锁。数据区 pread/pwrite 不走此锁, 多盘并行写不受影响。 */
    pthread_mutex_t lock;
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

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g->lock, &mattr);
    pthread_mutexattr_destroy(&mattr);

    for (int i = 0; i < n; i++) {
        rsdk_err_t rc = rsdk_dev_open(paths[i], &g->devs[i]);
        if (rc) {
            for (int k = 0; k < i; k++) { rsdk_dev_close(g->devs[k]); free(g->paths[k]); }
            pthread_mutex_destroy(&g->lock);
            free(g->devs); free(g->paths); free(g->health_ok); free(g->bw); free(g);
            return rc;
        }
        rsdk_dev_bind_lock(g->devs[i], &g->lock);   /* 整组元数据共享一把锁 */
        g->paths[i]     = strdup(paths[i]);
        g->health_ok[i] = 1;   /* 初始: 健康/未知均放行 */
        g->bw[i]        = 0.0;
    }
    g->n = n;
    for (int i = 0; i < 32; i++) g->chn_last[i] = -1;
    *out = g;
    return RSDK_OK;
}

/* 盘组元数据锁(= 组内各盘共享锁): 供 rec 层跨原语的复合操作(段翻转/开关 writer)加锁。 */
void rsdk_group_lock(rsdk_group_t *g)   { if (g) pthread_mutex_lock(&g->lock); }
void rsdk_group_unlock(rsdk_group_t *g) { if (g) pthread_mutex_unlock(&g->lock); }

int         rsdk_group_count(rsdk_group_t *g)      { return g ? g->n : 0; }
rsdk_dev_t *rsdk_group_dev(rsdk_group_t *g, int i) { return (g && i>=0 && i<g->n) ? g->devs[i] : NULL; }

void rsdk_group_close(rsdk_group_t *g) {
    if (!g) return;
    for (int i = 0; i < g->n; i++) {
        rsdk_dev_close(g->devs[i]);
        free(g->paths[i]);
    }
    pthread_mutex_destroy(&g->lock);   /* 各盘已关(不再持共享锁) */
    free(g->devs); free(g->paths); free(g->health_ok); free(g->bw);
    free(g);
}

int rsdk_group_find_path(rsdk_group_t *g, const char *path) {
    if (!g || !path) return -1;
    int idx = -1;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < g->n; i++)
        if (g->paths[i] && strcmp(g->paths[i], path) == 0) { idx = i; break; }
    pthread_mutex_unlock(&g->lock);
    return idx;
}

rsdk_err_t rsdk_group_add_disk(rsdk_group_t *g, const char *path) {
    if (!g || !path) return RSDK_E_PARAM;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < g->n; i++)
        if (g->paths[i] && strcmp(g->paths[i], path) == 0) {
            pthread_mutex_unlock(&g->lock); return RSDK_OK;          /* 已在组内 */
        }
    rsdk_dev_t *dev = NULL;
    rsdk_err_t rc = rsdk_dev_open(path, &dev);   /* 未格式化/外来盘会失败 → 不入组 */
    if (rc) { pthread_mutex_unlock(&g->lock); return rc; }

    int nn = g->n + 1;
    /* 逐个 realloc 并即时赋回 g->(失败也保持数组有效, 只是多出容量); 全部就绪再 n++。 */
    rsdk_dev_t **nd = realloc(g->devs, (size_t)nn * sizeof *nd);        if (nd) g->devs = nd;
    char       **np = realloc(g->paths, (size_t)nn * sizeof *np);       if (np) g->paths = np;
    int         *nh = realloc(g->health_ok, (size_t)nn * sizeof *nh);   if (nh) g->health_ok = nh;
    double      *nb = realloc(g->bw, (size_t)nn * sizeof *nb);          if (nb) g->bw = nb;
    char        *pdup = strdup(path);
    if (!nd || !np || !nh || !nb || !pdup) {                            /* OOM: 回滚, 不入组 */
        free(pdup);
        rsdk_dev_close(dev);
        pthread_mutex_unlock(&g->lock);
        return RSDK_E_IO;
    }
    rsdk_dev_bind_lock(dev, &g->lock);           /* 与组共享同一把锁(与 group_open 一致) */
    g->devs[g->n]      = dev;
    g->paths[g->n]     = pdup;
    g->health_ok[g->n] = 1;
    g->bw[g->n]        = 0.0;
    g->n = nn;                                    /* 最后才发布: balance_pick 读到时数组已就绪 */
    pthread_mutex_unlock(&g->lock);
    return RSDK_OK;
}

/* 带外改盘(formatStorage 直写盘区)后原地重载整组: 逐盘重载运行态(SB/事件区镜像/索引映射),
 * 复位 balance 落盘游标。同一 group 指针不变 → router/playback/rec 等借用者即时看到清空后的盘态,
 * 免重启且不产生"新建 group 致读路径读旧句柄"的陈旧数据。调用方须先停写(writer 关闭)。 */
rsdk_err_t rsdk_group_reload(rsdk_group_t *g) {
    if (!g) return RSDK_E_PARAM;
    pthread_mutex_lock(&g->lock);
    rsdk_err_t rc = RSDK_OK;
    for (int i = 0; i < g->n; i++) {
        if (!g->devs[i]) continue;
        rsdk_err_t r = rsdk_dev_reload(g->devs[i]);
        if (r) rc = r;
    }
    for (int i = 0; i < 32; i++) g->chn_last[i] = -1;   /* 每通道落盘游标复位 */
    g->rr = 0;
    pthread_mutex_unlock(&g->lock);
    return rc;
}

/* ==================== 新 API ==================== */

/* SMART 健康刷新: 对每块盘读 SMART 并更新 health_ok[].
 * 固件按分钟级定时调用, 保持 SG_IO 离热选路径。 */
rsdk_err_t rsdk_group_smart_refresh(rsdk_group_t *g) {
    if (!g) return RSDK_E_PARAM;
    for (int i = 0; i < g->n; i++) {
        rsdk_smart_t s;
        /* SG_IO 离热路径, 不持锁读盘; 只在写 health_ok[] 时短暂持锁(与 balance_pick 读一致)。 */
        rsdk_err_t rc = rsdk_smart_read(g->paths[i], &s);
        if (rc == RSDK_OK) {
            pthread_mutex_lock(&g->lock);
            g->health_ok[i] = rsdk_smart_ok(&s);
            pthread_mutex_unlock(&g->lock);
        }
        /* rc != OK: 取不到 SMART → 保留现有状态 */
    }
    return RSDK_OK;
}

/* 报告段字节: 更新 EWMA 写带宽。由录像层在段结束时调用。
 * EWMA: bw[i] = bw[i]*0.7 + bytes*0.3 */
void rsdk_balance_report(rsdk_group_t *g, rsdk_dev_t *dev, uint64_t bytes) {
    if (!g || !dev) return;
    pthread_mutex_lock(&g->lock);
    for (int i = 0; i < g->n; i++) {
        if (g->devs[i] == dev) {
            g->bw[i] = g->bw[i] * 0.7 + (double)bytes * 0.3;
            break;
        }
    }
    pthread_mutex_unlock(&g->lock);
}

/* 测试钩子: 强制设置健康状态(绕过 SMART, 用于 image 文件上的测试) */
void rsdk_group_set_health(rsdk_group_t *g, int disk, int ok) {
    if (!g || disk < 0 || disk >= g->n) return;
    pthread_mutex_lock(&g->lock);
    g->health_ok[disk] = ok;
    pthread_mutex_unlock(&g->lock);
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
    /* 持组锁: 读各盘 sb(write_ptr/seq_epoch)、health_ok、bw, 写 chn_last/rr。递归锁 → 可被
     * 已持锁的 start_seg 复合调用。 */
    pthread_mutex_lock(&g->lock);

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
        /* ★ 偏带宽均衡(原 0.4/0.4/0.2 过重 fill → 两盘填充不均时全压空盘)。 */
        scores[i] = 0.55 * bw_norm_i + 0.30 * fill_i + 0.15 * pressure_i;
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

    /* 5. ★ 带宽均摊:通道固定 home 盘 = 候选集中的 chn%cand_n 位 → N 路均分到各盘**并发写**,用满
     *    多盘写带宽。修:原按"填充最低 + 通道亲和"选盘 → 两盘填充不均时所有并发写全压空盘,第二块盘
     *    带宽白费(真机:2 盘 sda 写 31万扇区 / sdb 仅 536=纯元数据,load 54 单盘瓶颈)。仅当 home 盘
     *    "明显更差"(score 超候选最低 0.25,通常=近满/带宽过高)才让给更闲的盘,兼顾长期填充均衡。 */
    int slot = chn & 31;
    int home = cand[(chn >= 0 ? chn : 0) % cand_n];
    int pick = -1;
    if (scores[home] <= min_score + 0.25) pick = home;

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
    pthread_mutex_unlock(&g->lock);
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

int rsdk_group_foreach_stream(rsdk_group_t *g, uint32_t t0, uint32_t t1, int chn,
                              int rectype, int stream, rsdk_seg_visit_fn cb, void *user) {
    if (!g || !cb) return 0;
    int total = 0;
    /* 覆盖统计与盘间顺序无关, 无需跨盘归并/排序; 各盘独立遍历累加即可。 */
    for (int i = 0; i < g->n; i++)
        total += rsdk_index_foreach_stream(g->devs[i], t0, t1, chn, rectype, stream, cb, user);
    return total;
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

/* 按墙钟 epoch 定位:选覆盖 wall 的段,再段内读 RK_KEYIDX 表跳到 ≤wall 的最近 IDR。
 * 大段(3600s/近满 chunk)必须用它——否则从段头顺读几万帧才到 wall(回放"无录像"根因)。 */
rsdk_err_t rsdk_group_play_seek(rsdk_group_player_t *p, uint32_t wall) {
    if (!p || p->nseg <= 0) return RSDK_E_PARAM;
    int idx = 0, found = -1;
    for (int i = 0; i < p->nseg; i++) {
        uint32_t e = (p->segs[i].end_time == 0xFFFFFFFFu) ? 0xFFFFFFFFu : p->segs[i].end_time;
        if (p->segs[i].start_time <= wall && wall <= e) { found = i; break; }
        if (p->segs[i].start_time <= wall) idx = i;   /* 兜底:最后一个起点≤wall(落 gap 时) */
    }
    p->cur = (found >= 0) ? found : idx;
    p->seek_pending = 0;      /* 不走 pts seek,改用段内墙钟 seek */
    p->have_prev = 0;
    rsdk_err_t rc = open_cur(p);
    if (rc) return rc;
    if (p->pl) rsdk_play_seek(p->pl, wall);   /* 段内 RK_KEYIDX 直达 ≤wall 的 IDR */
    return RSDK_OK;
}

void rsdk_group_play_close(rsdk_group_player_t *p) {
    if (!p) return;
    if (p->pl) rsdk_play_close(p->pl);
    free(p->segs); free(p);
}
