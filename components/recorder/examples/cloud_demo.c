/* cloud_demo — rsdk_cloud 云存状态 API 断言测试。
 * 覆盖: make_event_id / event_begin(upsert) / add_seg / set_state(attempts++) /
 *       get / enumerate_pending(PENDING/FAILED/stale) / on_reclaim(→LOST)。 */
#include "rsdk.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(c, m) do{ if(!(c)){ printf("FAIL: %s\n", m); g_fail++; } else printf("ok: %s\n", m); }while(0)

int main(void)
{
    const char *db = "/tmp/rsdk_cloud_test.db";
    unlink(db); unlink("/tmp/rsdk_cloud_test.db-wal"); unlink("/tmp/rsdk_cloud_test.db-shm");

    void *meta = NULL;
    CHECK(rsdk_meta_open(db, &meta) == RSDK_OK && meta, "meta_open");
    CHECK(rsdk_meta_db(meta) != NULL, "meta_db handle exposed");

    /* 事件 1: human @ ch1 starttime=1711683198 */
    uint64_t e1 = rsdk_cloud_make_event_id(1, 1711683198u, RSDK_REC_HUMAN, 0);
    uint64_t e1b = rsdk_cloud_make_event_id(1, 1711683198u, RSDK_REC_HUMAN, 0);
    CHECK(e1 == e1b, "make_event_id deterministic");

    rsdk_cloud_event_t ev; memset(&ev, 0, sizeof(ev));
    ev.event_id = e1; ev.chn = 1; ev.rectype = RSDK_REC_HUMAN;
    ev.starttime = 1711683198u; ev.state = RSDK_CLOUD_PENDING;
    ev.seg_id = 100; ev.disk = 0; ev.start_chunk = 5000;
    CHECK(rsdk_cloud_event_begin(meta, &ev) == RSDK_OK, "event_begin insert");

    /* 幂等: 再 begin 一次不应新增行 */
    CHECK(rsdk_cloud_event_begin(meta, &ev) == RSDK_OK, "event_begin idempotent");

    rsdk_cloud_event_t got;
    CHECK(rsdk_cloud_get(meta, e1, &got) == RSDK_OK, "get e1");
    CHECK(got.state == RSDK_CLOUD_PENDING && got.chn == 1 && got.start_chunk == 5000, "e1 fields");
    CHECK(got.attempts == 0, "e1 attempts==0");

    /* 绑定第二个段（不同 chunk） */
    rsdk_index_slot_t seg; memset(&seg, 0, sizeof(seg));
    seg.seg_id = 101; seg.start_disk = 0; seg.start_chunk = 5001; seg.end_time = 1711683205u;
    CHECK(rsdk_cloud_event_add_seg(meta, e1, &seg) == RSDK_OK, "add_seg");

    /* 事件 2: vehicle @ ch2, 先 PENDING 再 FAILED */
    uint64_t e2 = rsdk_cloud_make_event_id(2, 1711683300u, RSDK_REC_VEHICLE, 0);
    rsdk_cloud_event_t ev2 = ev; ev2.event_id = e2; ev2.chn = 2; ev2.rectype = RSDK_REC_VEHICLE;
    ev2.starttime = 1711683300u; ev2.seg_id = 200; ev2.start_chunk = 6000;
    rsdk_cloud_event_begin(meta, &ev2);

    /* enumerate: 应含 e1,e2 两条 PENDING */
    rsdk_cloud_event_t list[8];
    rsdk_cloud_poll_opt_t opt = { .include_failed = 1, .stale_uploading_s = 0, .chn = -1 };
    int n = rsdk_cloud_enumerate_pending(meta, &opt, list, 8);
    CHECK(n == 2, "enumerate 2 pending");
    CHECK(list[0].event_id == e1, "enumerate ordered by starttime (e1 first)");

    /* e1 → UPLOADING (attempts→1) → DONE */
    CHECK(rsdk_cloud_set_state(meta, e1, RSDK_CLOUD_UPLOADING, 0) == RSDK_OK, "e1 uploading");
    rsdk_cloud_get(meta, e1, &got);
    CHECK(got.attempts == 1, "attempts incremented on UPLOADING");
    CHECK(rsdk_cloud_set_state(meta, e1, RSDK_CLOUD_DONE, 0) == RSDK_OK, "e1 done");

    /* e2 → FAILED with err -5 */
    rsdk_cloud_set_state(meta, e2, RSDK_CLOUD_FAILED, -5);
    rsdk_cloud_get(meta, e2, &got);
    CHECK(got.state == RSDK_CLOUD_FAILED && got.last_err == -5, "e2 failed err");

    /* enumerate include_failed=1 → 只剩 e2(FAILED)，e1 已 DONE */
    n = rsdk_cloud_enumerate_pending(meta, &opt, list, 8);
    CHECK(n == 1 && list[0].event_id == e2, "enumerate after done: only e2");

    /* include_failed=0 → 0 条 */
    opt.include_failed = 0;
    n = rsdk_cloud_enumerate_pending(meta, &opt, list, 8);
    CHECK(n == 0, "enumerate no-failed: 0");

    /* on_reclaim: 覆盖 e2 起始 chunk(6000,disk0) → e2 变 LOST */
    int marked = rsdk_cloud_on_reclaim(meta, 0, 6000);
    CHECK(marked == 1, "on_reclaim marked 1");
    rsdk_cloud_get(meta, e2, &got);
    CHECK(got.state == RSDK_CLOUD_LOST, "e2 → LOST");

    /* on_reclaim 命中 e1 的第二段 chunk(5001) 但 e1 已 DONE → 不改 */
    marked = rsdk_cloud_on_reclaim(meta, 0, 5001);
    CHECK(marked == 0, "on_reclaim skips DONE event");

    rsdk_meta_close(meta);
    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
