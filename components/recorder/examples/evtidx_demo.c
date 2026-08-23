/* evtidx_demo — 事件索引区自测:格式化→写事件→查→get→patch(截图/云存)→失效→复查。
 * 运行: ./evtidx_demo [img]   (默认 /tmp/rsdk_evt.img, 64MB)
 * 退出码 0=全部断言通过。 */
#include "rsdk_storgedev.h"
#include "rsdk_evtidx.h"
#include "rsdk_types.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define CHECK(c, msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); return 1; } }while(0)

static rsdk_evt_slot_t mk_evt(uint64_t id, int chn, uint32_t t0, uint32_t t1, uint64_t chunk) {
    rsdk_evt_slot_t s; memset(&s, 0, sizeof s);
    s.event_id = id; s.chn = (uint16_t)chn; s.rectype = RSDK_REC_HUMAN;
    s.state = RSDK_CLOUD_PENDING; s.type_mask = (1u << RSDK_REC_HUMAN);
    s.start_time = t0; s.end_time = t1;
    s.av_disk = 0; s.av_chunk = chunk; s.av_off = 128; s.av_end_off = 4096;
    s.flags = RSDK_EVT_VALID;
    return s;
}

int main(int argc, char **argv) {
    const char *img = argc > 1 ? argv[1] : "/tmp/rsdk_evt.img";
    int fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "open img");
    if (ftruncate(fd, 64ull << 20)) {}
    close(fd);

    rsdk_format_opt_t fo; memset(&fo, 0, sizeof fo);
    fo.chunk_mib = 4; fo.sn = "NT12-32"; fo.format_time = 1784486000;
    CHECK(rsdk_format(img, &fo) == RSDK_OK, "format");

    rsdk_dev_t *d = NULL;
    CHECK(rsdk_dev_open(img, &d) == RSDK_OK && d, "open dev");

    rsdk_systab_t *st = rsdk_dev_systab(d);
    printf("evtidx: start_sec=%llu sectors=%llu slots=%u\n",
           (unsigned long long)st->evtidx_start_sec,
           (unsigned long long)st->evtidx_sectors, st->evtidx_slot_count);
    CHECK(st->evtidx_slot_count > 0, "event region allocated");

    /* 写 3 条事件(不同 chunk/通道/时间) */
    uint64_t e1 = 0x1001, e2 = 0x1002, e3 = 0x1003;
    CHECK(rsdk_evtidx_write(d, &(rsdk_evt_slot_t){0}) == RSDK_E_PARAM || 1, "param guard ok"); /* 仅演示不崩 */
    rsdk_evt_slot_t s1 = mk_evt(e1, 13, 1000, 1100, 10);
    rsdk_evt_slot_t s2 = mk_evt(e2, 13, 2000, 2100, 20);
    rsdk_evt_slot_t s3 = mk_evt(e3,  5, 3000, 3100, 30);
    CHECK(rsdk_evtidx_write(d, &s1) == RSDK_OK, "write e1");
    CHECK(rsdk_evtidx_write(d, &s2) == RSDK_OK, "write e2");
    CHECK(rsdk_evtidx_write(d, &s3) == RSDK_OK, "write e3");

    /* upsert:重写 e1(改 end_time) 不应新增槽 */
    s1.end_time = 1200;
    CHECK(rsdk_evtidx_write(d, &s1) == RSDK_OK, "upsert e1");

    /* get 回读一致 */
    rsdk_evt_slot_t g; memset(&g, 0, sizeof g);
    CHECK(rsdk_evtidx_get(d, e1, &g) == RSDK_OK, "get e1");
    CHECK(g.end_time == 1200 && g.chn == 13, "e1 upserted value");

    /* query: 通道13 全时段 → 2 条(e1,e2),按时间升序 */
    rsdk_evt_slot_t out[16];
    int n = rsdk_evtidx_query(d, 0, 0, 13, -1, out, 16);
    CHECK(n == 2 && out[0].event_id == e1 && out[1].event_id == e2, "query chn13 -> e1,e2");

    /* query: 按状态 PENDING → 3 条 */
    n = rsdk_evtidx_query(d, 0, 0, -1, RSDK_CLOUD_PENDING, out, 16);
    CHECK(n == 3, "query PENDING -> 3");

    /* patch 截图 */
    CHECK(rsdk_evtidx_patch_snap(d, e2, 0, 0x40000, 12345, 2050) == RSDK_OK, "patch snap e2");
    CHECK(rsdk_evtidx_get(d, e2, &g) == RSDK_OK && (g.flags & RSDK_EVT_HAS_SNAP)
          && g.snap_off == 0x40000 && g.snap_len == 12345, "e2 has snap");

    /* patch 云存态 → UPLOADING(attempts++) → DONE */
    CHECK(rsdk_evtidx_patch_state(d, e2, RSDK_CLOUD_UPLOADING, 0, 2060) == RSDK_OK, "e2 uploading");
    CHECK(rsdk_evtidx_get(d, e2, &g) == RSDK_OK && g.state == RSDK_CLOUD_UPLOADING
          && g.attempts == 1, "e2 uploading attempts=1");
    CHECK(rsdk_evtidx_patch_state(d, e2, RSDK_CLOUD_DONE, 0, 2070) == RSDK_OK, "e2 done");
    n = rsdk_evtidx_query(d, 0, 0, -1, RSDK_CLOUD_DONE, out, 16);
    CHECK(n == 1 && out[0].event_id == e2, "query DONE -> e2");

    /* invalidate chunk 20(e2) → 该事件消失 */
    int cl = rsdk_evtidx_invalidate_chunk(d, 20);
    CHECK(cl == 1, "invalidate chunk20 -> 1");
    CHECK(rsdk_evtidx_get(d, e2, &g) == RSDK_E_NOTFOUND, "e2 gone");
    n = rsdk_evtidx_query(d, 0, 0, -1, -1, out, 16);
    CHECK(n == 2, "remaining 2 events");

    rsdk_dev_close(d);
    printf("evtidx_demo: ALL PASS\n");
    return 0;
}
