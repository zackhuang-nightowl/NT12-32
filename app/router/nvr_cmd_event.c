/***************************************************************************************
 *  nvr_cmd_event.c — event/回放检索 域 handler:事件列表 + 连续录像月历/日内时间轴。
 *  全部接 rsdk 录像索引(rsdk_group_query)。★ 通道键统一 0-based(录像 writer 存的是内部
 *  0-based chn,见 nvr_channel.c "内部 0-based");协议 channel 1-based → chn0=channel-1。
 *  日历/时间轴按**本地时区**(mktime)换 epoch —— 录像 start_time 是 UTC epoch,GUI 日历是本地。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include "nvr_cmd_util.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
/* args.channels[](1-based)→ 0-based 过滤列表(日历/时间轴是**合并**:选中任一通道那天/那小时
 * 有录像即计入)。无 channels 数组 → 单元素 -1(全通道)。返回个数(≤max)。 */
static int chan_list0(cJSON *a, int *out, int max)
{
    cJSON *chs = cJSON_GetObjectItem(a, "channels");
    int n = 0;
    if (cJSON_IsArray(chs)) {
        int sz = cJSON_GetArraySize(chs);
        for (int i = 0; i < sz && n < max; i++) {
            int c1 = (int)cJSON_GetNumberValue(cJSON_GetArrayItem(chs, i));
            if (c1 > 0) out[n++] = c1 - 1;   /* 1-based → 0-based */
        }
    }
    if (n == 0 && max > 0) out[n++] = -1;   /* 无选择 → 全通道(-1) */
    return n;
}

char *cmd_X_NightOwl_queryEventList(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int ch1 = nvr_jint(a, "channel", -1);
    int ch  = (ch1 > 0) ? ch1 - 1 : -1;   /* 0-based;-1=全通道 */
    uint32_t t0 = (uint32_t)nvr_jint(a, "startTime", 0), t1 = (uint32_t)nvr_jint(a, "endTime", 0);
    if (t1 == 0) t1 = (uint32_t)time(NULL);
    cJSON *o = cJSON_CreateObject(); cJSON *arr = cJSON_AddArrayToObject(o, "events");
    if (c->group) { rsdk_index_slot_t sl[128]; int n = rsdk_group_query(c->group, t0, t1, ch, -1, sl, 128);
        for (int i = 0; i < n && i < 128; i++) { if (!(sl[i].flags & RSDK_SLOT_EVENT)) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "channel", sl[i].chn + 1);   /* 回协议边界 1-based */
            cJSON_AddNumberToObject(e, "startTime", sl[i].start_time);
            cJSON_AddNumberToObject(e, "endTime", sl[i].end_time);
            cJSON_AddNumberToObject(e, "type", sl[i].rectype);
            cJSON_AddItemToArray(arr, e); } }
    return nvr_resp_content(o);
}

/* 连续录像月历:resolution=day → 返回该月有录像的**天**列表;month → 该年有录像的**月**列表。 */
char *cmd_X_NightOwl_queryContinuousCalendar(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *res = nvr_jstr(a, "resolution", "day");
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);   /* 合并所有选中通道 */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "resolution", res);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    if (c->group && year > 0) {
        if (strcmp(res, "month") == 0) {
            /* 每通道单次年范围查询 + 按月分桶(避免 12 次索引扫描超时)。cap 1024 段/通道;
             * 超长留存可能截断 → 只影响“哪些月有录像”的完整性,日分辨率(下面 else)不受影响。 */
            uint32_t y0 = local_ymd_epoch(year, 1, 1, 0, 0, 0);
            uint32_t y1 = local_ymd_epoch(year, 12, 31, 23, 59, 59);
            rsdk_index_slot_t *sl = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * 1024);
            if (sl) {
                char mhit[13] = { 0 }; uint32_t nowt = (uint32_t)time(NULL);
                for (int ci = 0; ci < nch; ci++) {   /* 合并:任一通道命中即计入 */
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
            for (int ci = 0; ci < nch; ci++) {   /* 合并:任一通道那天有录像即计入 */
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
 * 分钟 m(0..59)→ 位 (m+4)(低 4 位不用),置位=该分钟有连续录像。整点全录=0xFFFFFFFFFFFFFFF0=-16。
 * 合并:选中任一通道那分钟有录像即置位。Int64 用 cJSON_CreateRaw 原样输出(避免 double 丢精度)。 */
char *cmd_X_NightOwl_queryRecordingInterval(cJSON *a, const nvr_cmd_ctx_t *c)
{
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0), day = nvr_jint(a, "day", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);   /* 合并所有选中通道 */
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
                int m0 = (int)((s0 - d0) / 60);          /* 起始分钟(含) */
                int m1 = (int)((s1 - 1 - d0) / 60);      /* 结束分钟(含) */
                for (int m = m0; m <= m1 && m < 1440; m++) {
                    if (m < 0) continue;
                    int h = m / 60, mm = m % 60;         /* 分钟 mm → 位 mm+4 */
                    mask[h] |= (1ULL << (mm + 4));
                }
            }
        }
    }
    cJSON *o = cJSON_CreateObject(); cJSON *list = cJSON_AddArrayToObject(o, "list");
    for (int h = 0; h < 24; h++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)mask[h]);  /* Int64 原样 */
        cJSON_AddItemToArray(list, cJSON_CreateRaw(buf));
    }
    return nvr_resp_content(o);
}

/* 事件月历:resolution=month → 该年有事件的月;=day → 该月有事件的日。
 * 事件段在盘上以 RSDK_SLOT_EVENT 标记(见 queryEventList),故按 rectype=-1 查后过滤 EVENT 标志。 */
char *cmd_X_NightOwl_queryEventCalendar(cJSON *a, const nvr_cmd_ctx_t *c)
{
    const char *res = nvr_jstr(a, "resolution", "day");
    int year = nvr_jint(a, "year", 0), month = nvr_jint(a, "month", 0);
    int chs0[64]; int nch = chan_list0(a, chs0, 64);   /* 合并所有选中通道 */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "resolution", res);
    cJSON *list = cJSON_AddArrayToObject(o, "list");
    if (c->group && year > 0) {
        uint32_t nowt = (uint32_t)time(NULL);
        if (strcmp(res, "month") == 0) {
            uint32_t y0 = local_ymd_epoch(year, 1, 1, 0, 0, 0);
            uint32_t y1 = local_ymd_epoch(year, 12, 31, 23, 59, 59);
            rsdk_index_slot_t *sl = (rsdk_index_slot_t *)malloc(sizeof(rsdk_index_slot_t) * 1024);
            if (sl) {
                char mhit[13] = { 0 };
                for (int ci = 0; ci < nch; ci++) {   /* 合并:任一通道那月有事件即计入 */
                    int n = rsdk_group_query(c->group, y0, y1, chs0[ci], -1, sl, 1024);
                    for (int i = 0; i < n; i++) {
                        if (!(sl[i].flags & RSDK_SLOT_EVENT)) continue;   /* 只算事件段 */
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
            for (int ci = 0; ci < nch; ci++) {   /* 合并:任一通道那天有事件即计入 */
                int n = rsdk_group_query(c->group, mt0, mt1, chs0[ci], -1, sl, 256);
                for (int i = 0; i < n; i++) {
                    if (!(sl[i].flags & RSDK_SLOT_EVENT)) continue;   /* 只算事件段 */
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
