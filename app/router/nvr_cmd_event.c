/***************************************************************************************
 *  nvr_cmd_event.c — event/回放检索 域 handler:事件列表 + 连续录像月历/日内时间轴。
 *  全部接 rsdk 录像索引(rsdk_group_query)。★ 通道键统一 0-based(录像 writer 存的是内部
 *  0-based chn,见 nvr_channel.c "内部 0-based");协议 channel 1-based → chn0=channel-1。
 *  日历/时间轴按**本地时区**(mktime)换 epoch —— 录像 start_time 是 UTC epoch,GUI 日历是本地。
 *
 *  事件来源(优先顺序):
 *    1) meta.db 中 RSDK_DOC_CLOUD 行(record_sched 触发时登记,含 rectype/starttime)
 *    2) 连续轨内联 RSDK_RK_EVENT 标记(mark_event)扫描 — meta 缺失时的兜底
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include "nvr_event.h"
#include "rsdk_meta.h"
#include "rsdk_types.h"
#include "rsdk_balance.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef NVR_STREAM_MAIN
#define NVR_STREAM_MAIN 0
#endif

static void local_tm(time_t tt, struct tm *out)
{
#if defined(_WIN32)
    localtime_s(out, &tt);
#else
    localtime_r(&tt, out);
#endif
}

/* 本地 年月日[时分秒] → epoch(用当前时区;isdst=-1 让 libc 判夏令)。 */
static uint32_t local_ymd_epoch(int y, int mo, int d, int h, int mi, int s)
{
    struct tm tm; memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}
static int days_in_month(int y, int mo)
{
    static const int dm[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int d = dm[(mo - 1) % 12];
    if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) d = 29;
    return d;
}
/* args.channels[](1-based)→ 0-based 过滤列表。无 channels 数组 → 单元素 -1(全通道)。 */
static int chan_list0(cJSON *a, int *out, int max)
{
    cJSON *chs = cJSON_GetObjectItem(a, "channels");
    int n = 0;
    if (cJSON_IsArray(chs)) {
        int sz = cJSON_GetArraySize(chs);
        for (int i = 0; i < sz && n < max; i++) {
            int c1 = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(chs, i));
            if (c1 > 0) out[n++] = c1 - 1;
        }
    }
    if (n == 0 && max > 0) out[n++] = -1;
    return n;
}

/* RSDK_REC_* → NOP eventTypes 线缆名。 */
static const char *rectype_to_tag(int rectype)
{
    switch (rectype) {
        case RSDK_REC_MOTION:    return "pixelChange";
        case RSDK_REC_HUMAN:     return "human";
        case RSDK_REC_FACE:      return "face";
        case RSDK_REC_VEHICLE:   return "vehicle";
        case RSDK_REC_LINECROSS: return "lineCross";
        case RSDK_REC_INTRUSION: return "fieldIntrusion";
        case RSDK_REC_ANIMAL:    return "animal";
        case RSDK_REC_PACKAGE:   return "package";
        case RSDK_REC_DOORBELL:  return "doorbellRing";
        default:                 return "pixelChange";
    }
}
static int tag_to_rectype(const char *tag)
{
    if (!tag) return -1;
    if (!strcmp(tag, "pixelChange") || !strcmp(tag, "motion") || !strcmp(tag, "pir"))
        return RSDK_REC_MOTION;
    if (!strcmp(tag, "human")) return RSDK_REC_HUMAN;
    if (!strcmp(tag, "face") || !strcmp(tag, "facialRecognition")) return RSDK_REC_FACE;
    if (!strcmp(tag, "vehicle")) return RSDK_REC_VEHICLE;
    if (!strcmp(tag, "lineCross")) return RSDK_REC_LINECROSS;
    if (!strcmp(tag, "fieldIntrusion")) return RSDK_REC_INTRUSION;
    if (!strcmp(tag, "animal")) return RSDK_REC_ANIMAL;
    if (!strcmp(tag, "package")) return RSDK_REC_PACKAGE;
    if (!strcmp(tag, "doorbellRing")) return RSDK_REC_DOORBELL;
    return -1;
}
static int type_mask_hit(uint32_t mask, int rectype)
{
    if (rectype <= 0 || rectype >= 32) return mask == 0;
    return (mask & (1u << rectype)) != 0 || mask == 0;
}
static int filter_types_hit(const int *want, int nw, int rectype, uint32_t type_mask)
{
    if (nw <= 0) return 1;   /* 无过滤 → 全收 */
    for (int i = 0; i < nw; i++) {
        if (want[i] < 0) continue;
        if (want[i] == rectype) return 1;
        if (type_mask_hit(type_mask, want[i])) return 1;
    }
    return 0;
}
static int chan_allowed(const int *chs0, int nch, int chn0)
{
    for (int i = 0; i < nch; i++) if (chs0[i] < 0 || chs0[i] == chn0) return 1;
    return 0;
}

typedef struct {
    int      chn0;
    uint32_t ts;
    uint32_t duration;
    int      rectype;
    uint32_t type_mask;
    uint64_t event_id;
} evt_rec_t;

static int evt_cmp_desc(const void *a, const void *b)
{
    const evt_rec_t *x = a, *y = b;
    if (x->ts > y->ts) return -1;
    if (x->ts < y->ts) return 1;
    return 0;
}

/* 从 meta DOC_CLOUD 收集事件。 */
static int collect_from_meta(void *meta, uint32_t t0, uint32_t t1,
                             const int *chs0, int nch,
                             const int *want, int nw,
                             evt_rec_t *out, int cap)
{
    if (!meta || cap <= 0) return 0;
    rsdk_meta_query_t q; memset(&q, 0, sizeof(q));
    q.t0 = t0; q.t1 = t1 ? t1 : 0xFFFFFFFFu; q.chn = -1;
    q.doc_type = RSDK_DOC_CLOUD; q.limit = 0;
    rsdk_metadoc_list_t lst; memset(&lst, 0, sizeof(lst));
    if (rsdk_meta_query(meta, &q, &lst) != RSDK_OK) return 0;
    int n = 0;
    for (int i = 0; i < lst.count && n < cap; i++) {
        const rsdk_metadoc_t *d = &lst.docs[i];
        int chn0 = (int)d->key.chn;
        if (!chan_allowed(chs0, nch, chn0)) continue;
        int rectype = RSDK_REC_MOTION;
        uint32_t start = d->key.ts, end = 0;
        if (d->json) {
            cJSON *j = cJSON_Parse(d->json);
            if (j) {
                cJSON *rt = cJSON_GetObjectItem(j, "rectype");
                cJSON *st = cJSON_GetObjectItem(j, "starttime");
                cJSON *en = cJSON_GetObjectItem(j, "end");
                if (cJSON_IsNumber(rt)) rectype = (int)rt->valuedouble;
                if (cJSON_IsNumber(st)) start = (uint32_t)st->valuedouble;
                if (cJSON_IsNumber(en)) end = (uint32_t)en->valuedouble;
                cJSON_Delete(j);
            }
        }
        uint32_t mask = (rectype > 0 && rectype < 32) ? (1u << rectype) : 0;
        if (!filter_types_hit(want, nw, rectype, mask)) continue;
        if (start < t0 || (t1 && start > t1)) continue;
        out[n].chn0 = chn0;
        out[n].ts = start;
        out[n].duration = (end > start) ? (end - start) : (uint32_t)NVR_EVT_POST_RECORD_S;
        out[n].rectype = rectype;
        out[n].type_mask = mask;
        out[n].event_id = d->key.event_id;
        n++;
    }
    rsdk_meta_free_list(&lst);
    return n;
}

/* 兜底:扫描连续轨 EVENT 标记(窗口不宜过大)。 */
static int collect_from_disk(rsdk_group_t *g, uint32_t t0, uint32_t t1,
                             const int *chs0, int nch,
                             const int *want, int nw,
                             evt_rec_t *out, int cap)
{
    if (!g || cap <= 0) return 0;
    int n = 0;
    rsdk_index_slot_t segs[128];
    for (int ci = 0; ci < nch && n < cap; ci++) {
        int ch = chs0[ci];
        int ns = rsdk_group_query_stream(g, t0, t1 ? t1 : t0 + 86400, ch,
                                         RSDK_REC_CONTINUOUS, NVR_STREAM_MAIN, segs, 128);
        if (ns <= 0)
            ns = rsdk_group_query_stream(g, t0, t1 ? t1 : t0 + 86400, ch,
                                         RSDK_REC_CONTINUOUS, -1, segs, 128);
        if (ns <= 0) continue;
        rsdk_group_player_t *gp = NULL;
        if (rsdk_group_play_open(g, segs, ns, &gp) != RSDK_OK || !gp) continue;
        for (int guard = 0; guard < 200000 && n < cap; guard++) {
            rsdk_frame_hdr_t h; const uint8_t *data = NULL; uint32_t len = 0; int disk = 0;
            if (rsdk_group_play_next(gp, &h, &data, &len, &disk) != RSDK_OK) break;
            if (h.rec_kind != RSDK_RK_EVENT) continue;
            uint32_t estart = (uint32_t)h.wall_time, eend = estart + NVR_EVT_POST_RECORD_S;
            uint32_t mask = 0; int rectype = (int)h.rectype;
            if (data && len >= sizeof(rsdk_mk_event_t)) {
                const rsdk_mk_event_t *mk = (const rsdk_mk_event_t *)data;
                estart = mk->event_start ? mk->event_start : estart;
                if (mk->event_end > estart) eend = mk->event_end;
                mask = mk->type_mask;
            }
            if (estart < t0 || (t1 && estart > t1)) continue;
            if (!filter_types_hit(want, nw, rectype, mask)) continue;
            /* 去重:同 event_id / 同秒同通道 */
            int dup = 0;
            for (int k = 0; k < n; k++) {
                if (out[k].event_id && h.event_id && out[k].event_id == h.event_id) { dup = 1; break; }
                if (out[k].chn0 == (int)h.chn && out[k].ts == estart) { dup = 1; break; }
            }
            if (dup) continue;
            out[n].chn0 = (int)h.chn;
            out[n].ts = estart;
            out[n].duration = (eend > estart) ? (eend - estart) : (uint32_t)NVR_EVT_POST_RECORD_S;
            out[n].rectype = rectype;
            out[n].type_mask = mask ? mask : ((rectype > 0 && rectype < 32) ? (1u << rectype) : 0);
            out[n].event_id = h.event_id;
            n++;
        }
        rsdk_group_play_close(gp);
    }
    return n;
}

char *cmd_X_NightOwl_queryEventList(cJSON *a, const nvr_cmd_ctx_t *c)
{
    uint32_t start = (uint32_t)nvr_jint(a, "startTime", 0);
    int query_id = nvr_jint(a, "queryID", 0);
    int list_num = nvr_jint(a, "listNumber", 30);
    if (list_num <= 0) list_num = 30;
    if (list_num > 500) list_num = 500;
    if (start == 0) start = (uint32_t)time(NULL);

    int chs0[64]; int nch = chan_list0(a, chs0, 64);
    int want[32], nw = 0;
    cJSON *ets = a ? cJSON_GetObjectItem(a, "eventTypes") : NULL, *it;
    if (cJSON_IsArray(ets)) cJSON_ArrayForEach(it, ets) {
        if (nw < 32 && cJSON_IsString(it)) {
            int rt = tag_to_rectype(it->valuestring);
            if (rt >= 0) want[nw++] = rt;
        }
    }

    /* latest: [0, startTime] 倒序取 listNumber */
    const int CAP = 2048;
    evt_rec_t *buf = (evt_rec_t *)calloc((size_t)CAP, sizeof(evt_rec_t));
    int n = 0;
    if (buf) {
        n = collect_from_meta(c->meta, 0, start, chs0, nch, want, nw, buf, CAP);
        if (n <= 0 && c->group)
            n = collect_from_disk(c->group, (start > 7 * 86400u) ? (start - 7 * 86400u) : 0,
                                  start, chs0, nch, want, nw, buf, CAP);
        if (n > 1) qsort(buf, (size_t)n, sizeof(evt_rec_t), evt_cmp_desc);
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "queryID", query_id);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    int emit = (n < list_num) ? n : list_num;
    for (int i = 0; i < emit; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON *types = cJSON_AddArrayToObject(e, "eventTypes");
        /* 优先按 type_mask 展开多类型;否则单 rectype */
        int any = 0;
        for (int b = 1; b < 32; b++) {
            if (buf[i].type_mask & (1u << b)) {
                cJSON_AddItemToArray(types, cJSON_CreateString(rectype_to_tag(b)));
                any = 1;
            }
        }
        if (!any) cJSON_AddItemToArray(types, cJSON_CreateString(rectype_to_tag(buf[i].rectype)));
        cJSON_AddNumberToObject(e, "timestamp", buf[i].ts);
        char fn[32]; snprintf(fn, sizeof(fn), "%u", buf[i].ts);
        cJSON_AddStringToObject(e, "fileName", fn);
        {
            char thumb[128] = "";
#if RSDK_CFG_METADATA
            if (buf[i].event_id && c->meta) {
                rsdk_pic_ref_t pr[1];
                int np = rsdk_pic_list_event(c->meta, buf[i].event_id, RSDK_PIC_MAIN, pr, 1);
                if (np <= 0) np = rsdk_pic_list_event(c->meta, buf[i].event_id, -1, pr, 1);
                if (np > 0)
                    /* 只填隧道 URL；App 经 TUTK 映射 8089 GET，不给 LAN 直连地址。 */
                    snprintf(thumb, sizeof(thumb),
                             "http://iotc-tunnel:8089/eventSnap?eid=%llu",
                             (unsigned long long)buf[i].event_id);
            }
#else
            (void)c;
#endif
            cJSON_AddStringToObject(e, "thumbnailUrl", thumb);
        }
        cJSON_AddNumberToObject(e, "channel", buf[i].chn0 + 1);
        cJSON_AddNumberToObject(e, "duration", buf[i].duration);
        cJSON_AddItemToArray(list, e);
    }
    free(buf);
    return nvr_resp_content(o);
}

/* 连续录像月历:resolution=day → 返回该月有录像的**天**列表;month → 该年有录像的**月**列表。 */
char *cmd_X_NightOwl_queryContinuousCalendar(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *res = nvr_jstr(a, "resolution", "day");
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "resolution", res);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    if (c->group && year > 0) {
        if (strcmp(res, "month") == 0) {
            uint32_t y0 = local_ymd_epoch(year, 1, 1, 0, 0, 0);
            uint32_t y1 = local_ymd_epoch(year, 12, 31, 23, 59, 59);
            rsdk_index_slot_t *sl = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * 1024);
            if (sl) {
                char mhit[13] = { 0 }; uint32_t nowt = (uint32_t)time(NULL);
                for (int ci = 0; ci < nch; ci++) {
                    int n = rsdk_group_query(c->group, y0, y1, chs0[ci], RSDK_REC_CONTINUOUS, sl, 1024);
                    for (int i = 0; i < n; i++) {
                        uint32_t s0 = sl[i].start_time;
                        uint32_t s1 = (sl[i].end_time == 0xFFFFFFFFu) ? nowt : sl[i].end_time;
                        for (int mo = 1; mo <= 12; mo++) {
                            uint32_t m0 = local_ymd_epoch(year, mo, 1, 0, 0, 0);
                            uint32_t m1 = local_ymd_epoch(year, mo, days_in_month(year, mo), 23, 59, 59);
                            if (s0 <= m1 && s1 >= m0) mhit[mo] = 1;
                        }
                    }
                }
                for (int mo = 1; mo <= 12; mo++) if (mhit[mo]) cJSON_AddItemToArray(list, cJSON_CreateNumber(mo));
                free(sl);
            }
        } else if (month > 0) {
            int dim = days_in_month(year, month);
            uint32_t mt0 = local_ymd_epoch(year, month, 1, 0, 0, 0);
            uint32_t mt1 = local_ymd_epoch(year, month, dim, 23, 59, 59);
            rsdk_index_slot_t sl[256];
            char dayhit[32] = { 0 };
            uint32_t nowt = (uint32_t)time(NULL);
            for (int ci = 0; ci < nch; ci++) {
                int n = rsdk_group_query(c->group, mt0, mt1, chs0[ci], RSDK_REC_CONTINUOUS, sl, 256);
                for (int i = 0; i < n; i++) {
                    uint32_t s0 = sl[i].start_time;
                    uint32_t s1 = (sl[i].end_time == 0xFFFFFFFFu) ? nowt : sl[i].end_time;
                    for (int d = 1; d <= dim; d++) {
                        uint32_t d0 = local_ymd_epoch(year, month, d, 0, 0, 0), d1 = d0 + 86400;
                        if (s0 < d1 && s1 >= d0) dayhit[d] = 1;
                    }
                }
            }
            for (int d = 1; d <= dim; d++) if (dayhit[d]) cJSON_AddItemToArray(list, cJSON_CreateNumber(d));
        }
    }
    return nvr_resp_content(o);
}

/* 日内录像时间轴:返回 24 元素 Int64 数组,list[h] 的每一位表示第 h 小时内的 1 分钟——
 * 分钟 m(0..59)→ 位 (m+4)(低 4 位不用),置位=该分钟有连续录像。 */
char *cmd_X_NightOwl_queryRecordingInterval(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0), day = nvr_jint(a, "day", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);
    uint64_t mask[24] = { 0 };
    if (c->group && year > 0 && month > 0 && day > 0) {
        uint32_t d0 = local_ymd_epoch(year, month, day, 0, 0, 0), d1 = d0 + 86400;
        rsdk_index_slot_t sl[256];
        uint32_t nowt = (uint32_t)time(NULL);
        for (int ci = 0; ci < nch; ci++) {
            int n = rsdk_group_query(c->group, d0, d1, chs0[ci], RSDK_REC_CONTINUOUS, sl, 256);
            for (int i = 0; i < n; i++) {
                uint32_t s0 = sl[i].start_time;
                uint32_t s1 = (sl[i].end_time == 0xFFFFFFFFu) ? nowt : sl[i].end_time;
                if (s0 < d0) s0 = d0;
                if (s1 > d1) s1 = d1;
                if (s1 <= s0) continue;
                int m0 = (int)((s0 - d0) / 60);
                int m1 = (int)((s1 - 1 - d0) / 60);
                for (int m = m0; m <= m1 && m < 1440; m++) {
                    if (m < 0) continue;
                    int h = m / 60, mm = m % 60;
                    mask[h] |= (1ULL << (mm + 4));
                }
            }
        }
    }
    cJSON *o = cJSON_CreateObject(); cJSON *list = cJSON_AddArrayToObject(o, "list");
    for (int h = 0; h < 24; h++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)mask[h]);
        cJSON_AddItemToArray(list, cJSON_CreateRaw(buf));
    }
    return nvr_resp_content(o);
}

/* 事件月历:基于 meta 云存事件 + 盘上 EVENT 标记(不再依赖 RSDK_SLOT_EVENT)。 */
char *cmd_X_NightOwl_queryEventCalendar(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *res = nvr_jstr(a, "resolution", "day");
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "resolution", res);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    if (year <= 0) return nvr_resp_content(o);

    uint32_t t0, t1;
    if (strcmp(res, "month") == 0) {
        t0 = local_ymd_epoch(year, 1, 1, 0, 0, 0);
        t1 = local_ymd_epoch(year, 12, 31, 23, 59, 59);
    } else {
        if (month <= 0) return nvr_resp_content(o);
        int dim = days_in_month(year, month);
        t0 = local_ymd_epoch(year, month, 1, 0, 0, 0);
        t1 = local_ymd_epoch(year, month, dim, 23, 59, 59);
    }

    const int CAP = 4096;
    evt_rec_t *buf = (evt_rec_t *)calloc((size_t)CAP, sizeof(evt_rec_t));
    int n = 0;
    if (buf) {
        n = collect_from_meta(c->meta, t0, t1, chs0, nch, NULL, 0, buf, CAP);
        if (n <= 0 && c->group)
            n = collect_from_disk(c->group, t0, t1, chs0, nch, NULL, 0, buf, CAP);
    }

    if (strcmp(res, "month") == 0) {
        char mhit[13] = { 0 };
        for (int i = 0; i < n; i++) {
            time_t tt = (time_t)buf[i].ts;
            struct tm tm; memset(&tm, 0, sizeof(tm)); local_tm(tt, &tm);
            if (tm.tm_year + 1900 == year && tm.tm_mon + 1 >= 1 && tm.tm_mon + 1 <= 12)
                mhit[tm.tm_mon + 1] = 1;
        }
        for (int mo = 1; mo <= 12; mo++) if (mhit[mo]) cJSON_AddItemToArray(list, cJSON_CreateNumber(mo));
    } else {
        char dayhit[32] = { 0 };
        for (int i = 0; i < n; i++) {
            time_t tt = (time_t)buf[i].ts;
            struct tm tm; memset(&tm, 0, sizeof(tm)); local_tm(tt, &tm);
            if (tm.tm_year + 1900 == year && tm.tm_mon + 1 == month &&
                tm.tm_mday >= 1 && tm.tm_mday <= 31)
                dayhit[tm.tm_mday] = 1;
        }
        int dim = days_in_month(year, month);
        for (int d = 1; d <= dim; d++) if (dayhit[d]) cJSON_AddItemToArray(list, cJSON_CreateNumber(d));
    }
    free(buf);
    return nvr_resp_content(o);
}

/* ---------- EventExtInfo（套包：NVR 本地库，不透传到相机） ---------- */

static int extinfo_cfg_on(const nvr_cmd_ctx_t *c, int chn0)
{
#if !RSDK_CFG_METADATA
    (void)c; (void)chn0; return 0;
#else
    if (!c || !c->meta) return 0;
    if (!c->settings) return 1;
    char k[72];
    snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.enable", chn0);
    int v = nvr_settings_get_int(c->settings, k, -1);
    if (v >= 0) return v != 0;
    return nvr_settings_get_int(c->settings, "ai.event_ext_info.enable", 1) != 0;
#endif
}

static int extinfo_cfg_ms(const nvr_cmd_ctx_t *c, int chn0)
{
    int ms = 1000;
    if (c && c->settings) {
        char k[80];
        snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.interval_ms", chn0);
        ms = nvr_settings_get_int(c->settings, k, 0);
        if (ms <= 0) ms = nvr_settings_get_int(c->settings, "ai.event_ext_info.interval_ms", 1000);
    }
    if (ms < 1000) ms = 1000;
    if (ms > 10000) ms = 10000;
    return ms;
}

char *cmd_AI_getEventExtInfoConfig(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enable", extinfo_cfg_on(c, ch1 - 1));
    cJSON_AddNumberToObject(o, "collectionIntervalMs", extinfo_cfg_ms(c, ch1 - 1));
    return nvr_resp_content(o);
}

char *cmd_AI_setEventExtInfoConfig(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    if (ch1 < 1) return nvr_resp_err("invalid_param");
    int chn0 = ch1 - 1;
    int has_en = nvr_jhas(a, "enable");
    int has_ms = nvr_jhas(a, "collectionIntervalMs");
    int en = nvr_jbool(a, "enable", 1);
    int ms = nvr_jint(a, "collectionIntervalMs", 1000);
    if (ms < 1000) ms = 1000;
    if (ms > 10000) ms = 10000;
    if (c && c->settings) {
        char k[80];
        if (has_en) {
            snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.enable", chn0);
            nvr_settings_set_int(c->settings, k, en);
        }
        if (has_ms) {
            snprintf(k, sizeof(k), "ai.event_ext_info.ch.%d.interval_ms", chn0);
            nvr_settings_set_int(c->settings, k, ms);
        }
    }
    if (c && c->cm) {
        nvr_channel_t ch;
        if (nvr_chan_get(c->cm, chn0, &ch) == 0 && ch.backend == 0 && ch.onvif_ip[0]) {
            int dev_ch = ch.dev_chn > 0 ? ch.dev_chn : 1;
            cJSON *fwd = cJSON_CreateObject();
            cJSON_AddNumberToObject(fwd, "channel", dev_ch);
            if (has_en) cJSON_AddBoolToObject(fwd, "enable", en);
            if (has_ms) cJSON_AddNumberToObject(fwd, "collectionIntervalMs", ms);
            char *aj = cJSON_PrintUnformatted(fwd);
            cJSON_Delete(fwd);
            if (aj) {
                char *resp = nvr_chan_dev_post(c->cm, chn0, "AI_setEventExtInfoConfig", aj);
                free(aj); free(resp);
            }
        }
    }
    return nvr_resp_ok();
}

char *cmd_AI_getEventExtInfo(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    uint32_t start = (uint32_t)nvr_jint(a, "startTime", 0);
    if (ch1 < 1 || start == 0) return nvr_resp_err("invalid_param");
#if !RSDK_CFG_METADATA
    (void)c;
    return nvr_resp_err("Not enabled");
#else
    if (!c || !c->meta) return nvr_resp_err("Not enabled");
    rsdk_meta_query_t q; memset(&q, 0, sizeof(q));
    q.t0 = start; q.t1 = start; q.chn = (int16_t)(ch1 - 1);
    q.doc_type = RSDK_DOC_AI_EVENT; q.limit = 1;
    rsdk_metadoc_list_t lst; memset(&lst, 0, sizeof(lst));
    cJSON *doc = NULL;
    if (rsdk_meta_query(c->meta, &q, &lst) == RSDK_OK && lst.count > 0 && lst.docs[0].json)
        doc = cJSON_Parse(lst.docs[0].json);
    rsdk_meta_free_list(&lst);
    if (!doc) {
        q.t0 = (start > 30u) ? (start - 30u) : 0;
        q.t1 = start + 30u; q.limit = 8;
        if (rsdk_meta_query(c->meta, &q, &lst) == RSDK_OK) {
            int best = -1; uint32_t best_d = 0xFFFFFFFFu;
            for (int i = 0; i < lst.count; i++) {
                uint32_t ts = lst.docs[i].key.ts;
                uint32_t d = (ts > start) ? (ts - start) : (start - ts);
                if (d < best_d) { best_d = d; best = i; }
            }
            if (best >= 0 && lst.docs[best].json)
                doc = cJSON_Parse(lst.docs[best].json);
            rsdk_meta_free_list(&lst);
        }
    }
    if (!doc) return nvr_resp_err("No entries found");
    return nvr_resp_content(doc);
#endif
}

#if defined(__has_include)
#  if __has_include(<zlib.h>)
#    include <zlib.h>
#    define NVR_HAVE_ZLIB 1
#  endif
#endif

#if RSDK_CFG_METADATA
#ifdef NVR_HAVE_ZLIB
static char *b64_encode(const unsigned char *src, size_t len)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;
    size_t i = 0, o = 0;
    while (i + 2 < len) {
        unsigned n = ((unsigned)src[i] << 16) | ((unsigned)src[i + 1] << 8) | src[i + 2];
        out[o++] = T[(n >> 18) & 63]; out[o++] = T[(n >> 12) & 63];
        out[o++] = T[(n >> 6) & 63];  out[o++] = T[n & 63];
        i += 3;
    }
    if (i < len) {
        unsigned n = (unsigned)src[i] << 16;
        if (i + 1 < len) n |= (unsigned)src[i + 1] << 8;
        out[o++] = T[(n >> 18) & 63]; out[o++] = T[(n >> 12) & 63];
        out[o++] = (i + 1 < len) ? T[(n >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}
#endif

static char *zlist_of_array(cJSON *list)
{
    char *raw = cJSON_PrintUnformatted(list);
    if (!raw) return NULL;
#ifdef NVR_HAVE_ZLIB
    uLong src_len = (uLong)strlen(raw);
    uLong dst_len = compressBound(src_len);
    unsigned char *zbuf = (unsigned char *)malloc(dst_len);
    char *b64 = NULL;
    if (zbuf && compress(zbuf, &dst_len, (const Bytef *)raw, src_len) == Z_OK)
        b64 = b64_encode(zbuf, (size_t)dst_len);
    free(zbuf);
    free(raw);
    return b64;
#else
    free(raw);
    return NULL;
#endif
}

static int evt_ext_cmp_desc(const void *a, const void *b)
{
    const rsdk_metadoc_t *x = a, *y = b;
    if (x->key.ts > y->key.ts) return -1;
    if (x->key.ts < y->key.ts) return 1;
    return 0;
}
#endif /* RSDK_CFG_METADATA */

char *cmd_AI_getEventExtInfoBatchByReverseTime(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", 1);
    uint32_t start = (uint32_t)nvr_jint(a, "startTime", 0);
    int list_num = nvr_jint(a, "listNumber", 100);
    int want_z = nvr_jbool(a, "zlist", 0);
    if (ch1 < 1 || start == 0) return nvr_resp_err("invalid_param");
    if (list_num <= 0) list_num = 100;
    if (list_num > 500) list_num = 500;
#if !RSDK_CFG_METADATA
    (void)c;
    return nvr_resp_err("Not enabled");
#else
    if (!c || !c->meta) return nvr_resp_err("Not enabled");
    rsdk_meta_query_t q; memset(&q, 0, sizeof(q));
    q.t0 = 0; q.t1 = start; q.chn = (int16_t)(ch1 - 1);
    q.doc_type = RSDK_DOC_AI_EVENT; q.limit = 0;
    rsdk_metadoc_list_t lst; memset(&lst, 0, sizeof(lst));
    if (rsdk_meta_query(c->meta, &q, &lst) != RSDK_OK)
        return nvr_resp_err("No Storage");
    if (lst.count > 1)
        qsort(lst.docs, (size_t)lst.count, sizeof(rsdk_metadoc_t), evt_ext_cmp_desc);
    int emit = (lst.count < list_num) ? lst.count : list_num;
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < emit; i++) {
        cJSON *doc = lst.docs[i].json ? cJSON_Parse(lst.docs[i].json) : NULL;
        if (!doc) {
            doc = cJSON_CreateObject();
            cJSON_AddNumberToObject(doc, "startTime", lst.docs[i].key.ts);
        }
        cJSON_AddItemToArray(arr, doc);
    }
    rsdk_meta_free_list(&lst);
    cJSON *o = cJSON_CreateObject();
    if (want_z) {
        char *zl = zlist_of_array(arr);
        cJSON_Delete(arr);
        if (zl) {
            cJSON_AddStringToObject(o, "zlist", zl);
            free(zl);
        } else {
            cJSON_AddStringToObject(o, "zlist", "");
        }
    } else {
        cJSON_AddItemToObject(o, "list", arr);
    }
    return nvr_resp_content(o);
#endif
}
