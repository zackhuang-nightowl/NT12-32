/* evtidx_newest_test — 事件查询必须"倒叙、从时间戳往回找":
 * 当命中数超过 cap 时,返回的应是**最新的 cap 条**(而非最旧的),
 * 且仍按 start_time 升序输出。复现并锁死 rsdk_evtidx_query 先截断后排序的 bug。
 * 运行: ./evtidx_newest_test [img]   退出码 0=全通过。 */
#include "rsdk_storgedev.h"
#include "rsdk_evtidx.h"
#include "rsdk_types.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define CHECK(c, msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); return 1; } }while(0)

static rsdk_evt_slot_t mk_evt(uint64_t id, int chn, uint32_t t0, uint64_t chunk) {
    rsdk_evt_slot_t s; memset(&s, 0, sizeof s);
    s.event_id = id; s.chn = (uint16_t)chn; s.rectype = RSDK_REC_HUMAN;
    s.state = RSDK_CLOUD_PENDING; s.type_mask = (1u << RSDK_REC_HUMAN);
    s.start_time = t0; s.end_time = t0 + 50;
    s.av_disk = 0; s.av_chunk = chunk; s.av_off = 128; s.av_end_off = 4096;
    s.flags = RSDK_EVT_VALID;
    return s;
}

int main(int argc, char **argv) {
    const char *img = argc > 1 ? argv[1] : "/tmp/rsdk_evt_newest.img";
    int fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "open img");
    if (ftruncate(fd, 64ull << 20)) {}
    close(fd);

    rsdk_format_opt_t fo; memset(&fo, 0, sizeof fo);
    fo.chunk_mib = 4; fo.sn = "NT12-32"; fo.format_time = 1784486000;
    CHECK(rsdk_format(img, &fo) == RSDK_OK, "format");

    rsdk_dev_t *d = NULL;
    CHECK(rsdk_dev_open(img, &d) == RSDK_OK && d, "open dev");
    CHECK(rsdk_dev_systab(d)->evtidx_slot_count >= 20, "region holds >=20 slots");

    /* 按写入顺序 = 时间升序写 20 条: start_time 1000,1100,...,2900 (chn=7) */
    const int N = 20;
    for (int i = 0; i < N; i++) {
        uint32_t t = 1000u + (uint32_t)i * 100u;
        rsdk_evt_slot_t s = mk_evt(0x2000u + (uint64_t)i, 7, t, 100u + (uint64_t)i);
        CHECK(rsdk_evtidx_write(d, &s) == RSDK_OK, "write event");
    }

    rsdk_evt_slot_t out[64];

    /* 1) newest_first=1,cap 小于命中数(cap=5,命中20)→ 返回**最新** 5 条,升序输出。
     *    最新 5 条的 start_time = 2500,2600,2700,2800,2900。 */
    int n = rsdk_evtidx_query(d, 0, 0, 7, -1, out, 5, 1);
    CHECK(n == 5, "cap=5 returns 5");
    CHECK(out[0].start_time == 2500, "newest-5 lower bound = 2500 (not oldest 1000)");
    CHECK(out[4].start_time == 2900, "newest-5 upper bound = 2900");
    for (int i = 1; i < n; i++)
        CHECK(out[i-1].start_time <= out[i].start_time, "ascending order preserved");

    /* 2) "从时间戳往回找":窗口 [0, t1=1550] + cap=3 → 最新的 ≤1550 三条 = 1300,1400,1500 */
    n = rsdk_evtidx_query(d, 0, 1550, 7, -1, out, 3, 1);
    CHECK(n == 3, "windowed cap=3 returns 3");
    CHECK(out[0].start_time == 1300 && out[2].start_time == 1500,
          "backward-from-1550 newest-3 = 1300,1400,1500");

    /* 3) cap 大于命中数 → 全量返回,升序(回归保护) */
    n = rsdk_evtidx_query(d, 0, 0, 7, -1, out, 64, 1);
    CHECK(n == N, "cap>=hits returns all");
    CHECK(out[0].start_time == 1000 && out[N-1].start_time == 2900, "full range ascending");

    /* 3b) newest_first=0(上传器 FIFO):cap=5 → 返回**最旧** 5 条 = 1000..1400 */
    n = rsdk_evtidx_query(d, 0, 0, 7, -1, out, 5, 0);
    CHECK(n == 5 && out[0].start_time == 1000 && out[4].start_time == 1400,
          "oldest-first (FIFO) cap=5 = 1000..1400");

    /* 4) 按通道收集的原语契约(collect_from_evtidx 依赖此下推),复刻真机场景:
     *    低频通道 chn=3 先写少量事件后即"停"(t=3000,3100,3200),随后高频邻居 chn=8
     *    持续写入远多于 cap 的更新事件(写入顺序 = 时间顺序,chn3 落在环形较旧位置)。
     *    · chn=-1 + 小 cap(旧 collect 的"全局最新再过滤")→ 全被 chn8 占满,拿不到 chn3;
     *    · chn=3 下推过滤 → 必得 chn3 自己的最新。 */
    for (int i = 0; i < 3; i++) {   /* 先:安静通道 chn3 */
        rsdk_evt_slot_t s = mk_evt(0x3000u + (uint64_t)i, 3, 3000u + (uint32_t)i * 100u, 300u + (uint64_t)i);
        CHECK(rsdk_evtidx_write(d, &s) == RSDK_OK, "write quiet-chn event");
    }
    for (int i = 0; i < 30; i++) {  /* 后:高频邻居 chn8(> cap 条,写入更晚=环形更新位) */
        rsdk_evt_slot_t s = mk_evt(0x8000u + (uint64_t)i, 8, 4000u + (uint32_t)i * 10u, 800u + (uint64_t)i);
        CHECK(rsdk_evtidx_write(d, &s) == RSDK_OK, "write busy-neighbor event");
    }
    n = rsdk_evtidx_query(d, 0, 0, -1, -1, out, 5, 1);   /* 全通道最新 5 条 */
    int seen3 = 0;
    for (int i = 0; i < n; i++) if (out[i].chn == 3) seen3 = 1;
    CHECK(n == 5 && !seen3, "chn=-1 newest-5 all busy-neighbor, excludes quiet chn3 (why per-chn pushdown needed)");
    n = rsdk_evtidx_query(d, 0, 0, 3, -1, out, 5, 1);    /* 下推 chn=3 */
    CHECK(n == 3 && out[0].start_time == 3000 && out[2].start_time == 3200,
          "pushdown chn=3 returns its newest regardless of busy neighbor");

    rsdk_dev_close(d);
    printf("evtidx_newest_test: ALL PASS\n");
    return 0;
}
