/***************************************************************************************
 *  multidisk_demo.c —— 多盘回放(设计 §4/§7.4)。
 *  两块盘各写若干段(时间上交错), 用 rsdk_group_query 跨盘归并按时间排序,
 *  rsdk_group_play 跨盘连续回放, 到段尾自动切盘; 校验逐字节一致 + 打印每段来自哪块盘。
 *
 *  运行:  ./multidisk_demo [imgA imgB]   (默认 /tmp/rsdk_A.img /tmp/rsdk_B.img, 各128MB)
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define NBURST   4
#define PERBURST 4
#define NFR      (NBURST*PERBURST)     /* 16 帧 */
static uint8_t *g_orig[NFR]; static uint32_t g_len[NFR];

static void make_frames(void) {
    for (int i = 0; i < NFR; i++) {
        uint32_t n = 50000 + (uint32_t)i*500;
        g_len[i] = n; g_orig[i] = malloc(n);
        for (uint32_t j = 0; j < n; j++) g_orig[i][j] = (uint8_t)(i*29 + j*13 + 7);
    }
}
static int mkimg(const char *p) {
    int fd = open(p, O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (ftruncate(fd, 128ull<<20)) { close(fd); return -1; }
    close(fd); return 0;
}

int main(int argc, char **argv) {
    const char *A = argc>2?argv[1]:"/tmp/rsdk_A.img";
    const char *B = argc>2?argv[2]:"/tmp/rsdk_B.img";
    printf("RSDK %s 多盘回放 | A=%s B=%s\n", rsdk_version(), A, B);

    rsdk_format_opt_t fo; memset(&fo,0,sizeof fo);
    fo.chunk_mib = 1; fo.sn = "NT12-32"; fo.format_time = 1784486000;
    if (mkimg(A)||mkimg(B)) { perror("mkimg"); return 1; }
    if (rsdk_format(A,&fo)||rsdk_format(B,&fo)) { printf("format 失败\n"); return 1; }

    const char *paths[2] = { A, B };
    rsdk_group_t *g;
    if (rsdk_group_open(paths, 2, &g) != RSDK_OK) { printf("group_open 失败\n"); return 1; }
    printf("盘组: %d 块盘\n", rsdk_group_count(g));

    /* ---- 写: 4 段交错落到两块盘 (段b → 盘 b%2), 时间递增 ---- */
    make_frames();
    for (int b = 0; b < NBURST; b++) {
        rsdk_dev_t *d = rsdk_group_dev(g, b % 2);
        rsdk_writer_t *w; rsdk_rec_open(d, 13, RSDK_REC_CONTINUOUS, &w);
        for (int k = 0; k < PERBURST; k++) {
            int i = b*PERBURST + k;
            rsdk_frame_t f = { .chn=13, .codec=RSDK_CODEC_H265,
                .frame_type=(k==0?RSDK_FRAME_I:RSDK_FRAME_P),
                .pts=(uint64_t)i*3000, .wall_time=1784486100+i, .data=g_orig[i], .len=g_len[i] };
            rsdk_rec_write_frame(w, &f);
        }
        rsdk_rec_close(w);
        printf("  段%d → 盘%d  (帧 %d..%d, t=%d..%d)\n", b, b%2,
               b*PERBURST, b*PERBURST+PERBURST-1, 1784486100+b*PERBURST, 1784486100+b*PERBURST+PERBURST-1);
    }

    /* ---- 跨盘归并检索(按时间升序) ---- */
    rsdk_index_slot_t segs[64];
    int ns = rsdk_group_query(g, 1784486000, 1784486999, 13, -1, segs, 64);
    printf("\n跨盘检索命中 %d 段(按时间升序, 标注来源盘):\n", ns);
    for (int s = 0; s < ns; s++)
        printf("  #%d seg_id=0x%06x 来自盘%d start_time=%u 帧数=%u\n",
               s, segs[s].seg_id, segs[s].start_disk, segs[s].start_time, segs[s].frame_count);

    /* ---- 跨盘连续回放 + 逐字节校验 ---- */
    rsdk_group_player_t *gp;
    if (rsdk_group_play_open(g, segs, ns, &gp) != RSDK_OK) { printf("group_play_open 失败\n"); return 1; }
    rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len; int disk, gi=0, bad=0, prev=-1;
    printf("\n跨盘连续回放:\n");
    while (rsdk_group_play_next(gp, &h, &data, &len, &disk) == RSDK_OK) {
        if (gi >= NFR) { bad++; break; }
        if (len != g_len[gi] || memcmp(data, g_orig[gi], len) != 0) bad++;
        if (disk != prev) { printf("  → 切到盘%d (从 pts=%llu 起)\n", disk, (unsigned long long)h.pts); prev = disk; }
        gi++;
    }
    rsdk_group_play_close(gp);
    printf("回放校验: 取回 %d/%d 帧, 逐字节%s\n", gi, NFR, bad?"有差异 ✗":"完全一致 ✓");

    /* ---- 跨盘全局 seek: 定位 pts=30000(应落在第3段/盘0) ---- */
    rsdk_group_play_open(g, segs, ns, &gp);
    rsdk_group_play_seek_pts(gp, 30000);
    if (rsdk_group_play_next(gp, &h, &data, &len, &disk) == RSDK_OK)
        printf("跨盘 seek(pts<=30000) → 盘%d pts=%llu wall=%u\n",
               disk, (unsigned long long)h.pts, (unsigned)h.wall_time);
    rsdk_group_play_close(gp);

    rsdk_group_close(g);
    for (int i=0;i<NFR;i++) free(g_orig[i]);
    printf("\n结果: %s\n", bad==0 ? "多盘回放 PASS (跨盘归并→连续回放→自动切盘→逐字节一致)" : "FAIL");
    return bad ? 1 : 0;
}
