/* rebuild_demo — 统一扫盘重建:写事件录像(帧+event标记+云存标记)+抓拍 → 抹掉事件索引区
 * → rsdk_scan_rebuild → 从事件索引区查回「音视频定位 + 截图指针 + 云存态」全链一致。
 * 运行: ./rebuild_demo [img]  (默认 /tmp/rsdk_rebuild.img, 64MB)。退出码 0=全过。 */
#include "rsdk_storgedev.h"
#include "rsdk_rec.h"
#include "rsdk_evtidx.h"
#include "rsdk_pic.h"
#include "rsdk_scan.h"
#include "rsdk_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); return 1; } }while(0)

int main(int argc, char **argv) {
    const char *img = argc > 1 ? argv[1] : "/tmp/rsdk_rebuild.img";
    int fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "open"); if (ftruncate(fd, 64ull << 20)) {} close(fd);

    rsdk_format_opt_t fo; memset(&fo, 0, sizeof fo);
    fo.chunk_mib = 4; fo.sn = "NT12-32"; fo.format_time = 1784486000;
    CHECK(rsdk_format(img, &fo) == RSDK_OK, "format");
    rsdk_dev_t *d = NULL;
    CHECK(rsdk_dev_open(img, &d) == RSDK_OK && d, "open dev");

    /* ---- 写一段事件录像: 打事件标签的连续帧 + 事件标记 + 云存"上传中"标记 ---- */
    uint64_t eid = 0xABCDEF01;
    uint32_t t0 = 1784486100, t1 = 1784486120;
    rsdk_writer_t *w;
    CHECK(rsdk_rec_open(d, 7, RSDK_REC_HUMAN, &w) == RSDK_OK, "rec_open");
    rsdk_rec_set_event(w, eid);                 /* 帧头打 event_id */
    uint64_t av_chunk = rsdk_rec_cur_chunk(w);
    uint8_t payload[4096]; memset(payload, 0xC5, sizeof payload);
    for (int i = 0; i < 8; i++) {
        rsdk_frame_t f = { .chn = 7, .stream = 0, .codec = RSDK_CODEC_H265,
            .frame_type = (i == 0 ? RSDK_FRAME_I : RSDK_FRAME_P),
            .pts = (uint64_t)i * 3000, .wall_time = t0 + i, .data = payload, .len = sizeof payload };
        CHECK(rsdk_rec_write_frame(w, &f) == RSDK_OK, "write frame");
    }
    rsdk_rec_mark_event(w, eid, RSDK_REC_HUMAN, t0, t1, (1u << RSDK_REC_HUMAN), 0);
    rsdk_rec_mark_cloud(w, eid, RSDK_CLOUD_UPLOADING, t0);   /* 崩溃前:上传中 */
    rsdk_rec_close(w);

    /* ---- 事件触发时刻抓拍 → MetaRegion blob + patch 事件槽截图指针 ---- */
    uint8_t jpeg[600]; memset(jpeg, 0x7A, sizeof jpeg); memcpy(jpeg, "\xFF\xD8\xFF", 3);
    rsdk_pic_key_t pk = { .chn = 7, .ts = t0, .event_id = eid, .type = RSDK_PIC_MAIN, .w = 320, .h = 240 };
    uint64_t pid = 0;
    CHECK(rsdk_pic_write(d, &pk, jpeg, sizeof jpeg, &pid) == RSDK_OK, "pic_write");

    /* ---- 正常路径也写一条事件槽(模拟运行期已建槽) ---- */
    rsdk_evt_slot_t live; memset(&live, 0, sizeof live);
    live.event_id = eid; live.chn = 7; live.rectype = RSDK_REC_HUMAN;
    live.state = RSDK_CLOUD_UPLOADING; live.start_time = t0; live.end_time = t1;
    live.av_chunk = av_chunk; live.av_off = 0; live.flags = RSDK_EVT_VALID;
    rsdk_evtidx_write(d, &live);

    /* ---- 抹掉整个事件索引区(模拟索引损坏),确认查不到 ---- */
    rsdk_systab_t *st = rsdk_dev_systab(d);
    uint8_t zero[512]; memset(zero, 0, sizeof zero);
    for (uint64_t s = 0; s < st->evtidx_sectors; s++)
        rsdk_rawdev_pwrite(rsdk_dev_raw(d), (st->evtidx_start_sec + s) * 512, zero, 512);
    st->evtidx_next = 0; rsdk_dev_flush(d);
    rsdk_dev_evtidx_reload(d);   /* 带外直写盘 → 重载内存镜像(模拟挂载时从损坏盘载入) */
    rsdk_evt_slot_t g;
    CHECK(rsdk_evtidx_get(d, eid, &g) == RSDK_E_NOTFOUND, "event gone after wipe");

    /* ---- 统一扫盘重建 ---- */
    int nseg = 0, nev = 0;
    CHECK(rsdk_scan_rebuild(d, NULL, &nseg, &nev) == RSDK_OK, "scan_rebuild");
    printf("rebuild: segs=%d events=%d\n", nseg, nev);

    /* ---- 全链校验:事件回来了 + 音视频定位 + 截图指针 + 云存态(上传中→待重试) ---- */
    CHECK(rsdk_evtidx_get(d, eid, &g) == RSDK_OK, "event recovered");
    CHECK(g.chn == 7 && g.rectype == RSDK_REC_HUMAN, "event chn/type");
    CHECK(g.av_chunk == av_chunk && (g.flags & RSDK_EVT_VALID), "av located");
    CHECK(g.state == RSDK_CLOUD_RETRY, "cloud UPLOADING -> RETRY on rebuild");
    CHECK((g.flags & RSDK_EVT_HAS_SNAP) && g.snap_off != 0 && g.snap_len > 0, "snapshot ptr recovered");
    printf("recovered: chn=%u type=%u av_chunk=%llu snap_off=%llu snap_len=%u state=%u(RETRY=4)\n",
           g.chn, g.rectype, (unsigned long long)g.av_chunk,
           (unsigned long long)g.snap_off, g.snap_len, g.state);

    /* ---- query 也能查到 ---- */
    rsdk_evt_slot_t out[8];
    int n = rsdk_evtidx_query(d, t0 - 10, t1 + 10, 7, -1, out, 8);
    CHECK(n == 1 && out[0].event_id == eid, "query recovered event");

    rsdk_dev_close(d);
    printf("rebuild_demo: ALL PASS\n");
    return 0;
}
