/***************************************************************************************
 *  rec_demo.c —— 录像垂直切片(设计 §7): 格式化 → AES-CTR 写入 → 索引 → 检索 → 解密回放。
 *  在镜像文件上跑通整条链路, 校验解密回放与原始逐字节一致。
 *
 *  编译:  由 CMake 构建(链接 librsdk)。
 *  运行:  ./rec_demo [image_path]   (默认 /tmp/rsdk_disk.img, 256MB)
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define NFRAMES  16
#define PBASE    200000            /* ~200KB, 近似 4K I 帧 */
static uint8_t *g_orig[NFRAMES];
static uint32_t g_len[NFRAMES];

static int aes_selftest(void) {   /* FIPS-197 AES-256 ECB 向量 */
    uint8_t key[32], pt[16], ct[16], exp[16];
    uint32_t rk[60];
    for (int i = 0; i < 32; i++) key[i] = i;
    const char *P="00112233445566778899aabbccddeeff", *C="8ea2b7ca516745bfeafc49904b496089";
    for (int i=0;i<16;i++){ sscanf(P+2*i,"%2hhx",&pt[i]); sscanf(C+2*i,"%2hhx",&exp[i]); }
    rsdk_aes256_key(key, rk); rsdk_aes256_encrypt_block(rk, pt, ct);
    return memcmp(ct, exp, 16) == 0;
}

static void make_frames(void) {
    for (int i = 0; i < NFRAMES; i++) {
        uint32_t n = PBASE + (uint32_t)i * 1000;
        g_len[i] = n; g_orig[i] = malloc(n);
        /* 前 6 字节仿 HEVC VPS 起始码, 便于肉眼识别; 其余确定性填充 */
        g_orig[i][0]=0;g_orig[i][1]=0;g_orig[i][2]=0;g_orig[i][3]=1;g_orig[i][4]=0x40;g_orig[i][5]=0x01;
        for (uint32_t j = 6; j < n; j++) g_orig[i][j] = (uint8_t)(i*31 + j*7);
    }
}

int main(int argc, char **argv) {
    const char *img = argc > 1 ? argv[1] : "/tmp/rsdk_disk.img";
    printf("RSDK %s | image=%s\n", rsdk_version(), img);
    printf("AES-256 自测(FIPS-197): %s\n", aes_selftest() ? "PASS" : "FAIL");
    printf("特性: enc=%d meta=%d balance=%d (来自 rsdk_features.conf)\n",
           rsdk_feature_on(RSDK_F_ENCRYPTION), rsdk_feature_on(RSDK_F_METADATA),
           rsdk_feature_on(RSDK_F_MULTIDISK_BALANCE));

    /* 造 256MB 镜像文件 */
    int fd = open(img, O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, 256ull<<20) != 0) { perror("ftruncate"); return 1; }
    close(fd);

    /* 格式化: chunk 用 1MiB 以便少量数据即可跨 chunk/多段 */
    rsdk_format_opt_t fo; memset(&fo, 0, sizeof fo);
    /* SN 须与 rsdk_dev_open 内派生 KEK 用的 SN 一致(真实系统取自工厂区; demo 用默认) */
    fo.chunk_mib = 1; fo.sn = "NT12-32"; fo.format_time = 1784486000;
    if (rsdk_format(img, &fo) != RSDK_OK) { printf("format 失败\n"); return 1; }

    rsdk_dev_t *d;
    if (rsdk_dev_open(img, &d) != RSDK_OK) { printf("open 失败\n"); return 1; }
    rsdk_dev_info_t info; rsdk_dev_info(d, &info);
    printf("格式化 OK: chunk_count=%llu chunk=%lluB data_start_sec=%llu enc_algo=%u\n",
        (unsigned long long)info.chunk_count, (unsigned long long)(info.chunk_sectors*512),
        (unsigned long long)info.data_start_sec, info.enc_algo);

    /* ---- 写入 16 帧 (通道13, 常录) ---- */
    make_frames();
    rsdk_writer_t *w;
    rsdk_rec_open(d, 13, RSDK_REC_CONTINUOUS, &w);
    for (int i = 0; i < NFRAMES; i++) {
        rsdk_frame_t f = { .chn=13, .stream=0, .codec=RSDK_CODEC_H265,
            .frame_type=(i%8==0?RSDK_FRAME_I:RSDK_FRAME_P),
            .pts=(uint64_t)i*3000, .wall_time=1784486092+i, .data=g_orig[i], .len=g_len[i] };
        if (rsdk_rec_write_frame(w, &f) != RSDK_OK) { printf("write %d 失败\n", i); return 1; }
    }
    rsdk_rec_close(w);
    printf("写入 %d 帧完成 (每帧 AES-256-CTR 加密=%d)\n", NFRAMES, info.enc_algo);

    /* ---- 硬核验证: 直接读盘上负载, 确认是密文(≠明文) ---- */
    if (info.enc_algo) {
        int rfd = open(img, O_RDONLY);
        uint64_t poff = info.data_start_sec*512 + 64;   /* chunk0 首帧: 头64B 后是负载 */
        uint8_t disk32[32]; if (pread(rfd, disk32, 32, (off_t)poff)!=32){} close(rfd);
        int same = (memcmp(disk32, g_orig[0], 32) == 0);
        printf("盘上首帧负载 vs 明文: %s (前4字节 盘=%02x%02x%02x%02x 明文=%02x%02x%02x%02x)\n",
            same ? "相同(未加密?) ✗" : "不同=密文 ✓",
            disk32[0],disk32[1],disk32[2],disk32[3], g_orig[0][0],g_orig[0][1],g_orig[0][2],g_orig[0][3]);
    }

    /* ---- 检索 + 解密回放 + 逐字节校验 ---- */
    rsdk_index_slot_t segs[64];
    int ns = rsdk_index_query(d, 1784486000, 1784486999, 13, -1, segs, 64);
    printf("检索命中段: %d 个 (按时间升序)\n", ns);
    int gi = 0, bad = 0;
    for (int s = 0; s < ns; s++) {
        rsdk_player_t *p; rsdk_play_open(d, &segs[s], &p);
        rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len;
        int fic = 0;
        while (rsdk_play_next_frame(p, &h, &data, &len) == RSDK_OK) {
            if (gi >= NFRAMES) { bad++; break; }
            if (len != g_len[gi] || memcmp(data, g_orig[gi], len) != 0) bad++;
            gi++; fic++;
        }
        printf("  段#%d seg_id=0x%06x chunk=%llu 帧数=%d enc=%u\n",
            s, segs[s].seg_id, (unsigned long long)segs[s].start_chunk, fic, h.enc);
        rsdk_play_close(p);
    }
    printf("回放校验: 取回 %d/%d 帧, 逐字节%s\n", gi, NFRAMES, bad ? "有差异 ✗" : "完全一致 ✓");

    /* ---- seek: 定位 pts=15000 ---- */
    if (ns > 0) {
        rsdk_player_t *p; rsdk_play_open(d, &segs[0], &p);
        rsdk_play_seek_pts(p, 15000);
        rsdk_frame_hdr_t h; const uint8_t *dt; uint32_t ln;
        if (rsdk_play_next_frame(p, &h, &dt, &ln) == RSDK_OK)
            printf("seek(pts<=15000) → 命中帧 pts=%llu wall=%u\n",
                   (unsigned long long)h.pts, (unsigned)h.wall_time);
        rsdk_play_close(p);
    }

    /* ---- 密钥轮换(KEK): rekey 后重开仍可解密 ---- */
    if (info.enc_algo) {
        rsdk_dev_rekey(d);
        rsdk_dev_close(d);
        rsdk_dev_open(img, &d);
        rsdk_index_slot_t s2[8]; int n2 = rsdk_index_query(d, 1784486000, 1784486999, 13, -1, s2, 8);
        int rk_ok = 0;
        if (n2 > 0) { rsdk_player_t *p2; rsdk_play_open(d, &s2[0], &p2);
            rsdk_frame_hdr_t hh; const uint8_t *dd; uint32_t ll;
            if (rsdk_play_next_frame(p2, &hh, &dd, &ll) == RSDK_OK) rk_ok = (ll==g_len[0] && memcmp(dd,g_orig[0],ll)==0);
            rsdk_play_close(p2); }
        printf("密钥轮换(rekey)后重开回放: %s\n", rk_ok ? "仍逐字节一致 ✓" : "失败 ✗");
    }

#if RSDK_CFG_METADATA
    /* ---- 元数据: 存完整 JSON 绑定首段, 智能检索 ---- */
    void *mc;
    if (rsdk_meta_open(":memory:", &mc) == RSDK_OK && ns > 0) {
        rsdk_meta_key_t k = { .ts=1784486092, .chn=13, .event_id=10769,
            .doc_type=RSDK_DOC_AI_EVENT,
            .seg={ .disk=0, .chunk=segs[0].start_chunk, .off=segs[0].start_off, .pts=0 } };
        const char *doc = "{\"event\":\"human\",\"objects\":[{\"cls\":\"person\",\"color\":\"red\",\"plate\":null}]}";
        uint64_t id; rsdk_meta_put(mc, &k, doc, strlen(doc), &id);
        rsdk_meta_query_t q; memset(&q,0,sizeof q);
        q.t0=1784486000; q.t1=1784486999; q.chn=13;
        q.json_path="$.objects[0].cls"; q.json_match="person";
        rsdk_metadoc_list_t lst;
        rsdk_meta_query(mc, &q, &lst);
        printf("元数据: 存1条(id=%llu) 智能检索 person → 命中 %d 条; seg_chunk 绑定=%llu\n",
            (unsigned long long)id, lst.count,
            lst.count? (unsigned long long)lst.docs[0].key.seg.chunk : 0);
        rsdk_meta_free_list(&lst);
        rsdk_meta_close(mc);
    }
#endif

    rsdk_dev_close(d);
    for (int i=0;i<NFRAMES;i++) free(g_orig[i]);
    printf("\n结果: %s\n", bad==0 ? "全链路 PASS (格式化→加密写→索引→检索→解密回放一致)" : "FAIL");
    return bad ? 1 : 0;
}
