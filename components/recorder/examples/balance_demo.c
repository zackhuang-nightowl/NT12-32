/***************************************************************************************
 *  balance_demo.c —— 多盘写入均衡 + 多盘回放闭环(设计 §4)。
 *  单个盘组写入器写一路录像; 每换新段按盘负载选盘, 把这一路均摊到多盘。
 *  再用 rsdk_group_query/rsdk_group_play 跨盘归并回放, 校验逐字节一致。
 *
 *  运行:  ./balance_demo [imgA imgB]   (默认 /tmp/rsdk_bA.img /tmp/rsdk_bB.img, 各128MB)
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define NFR 18
static uint8_t *g_orig[NFR]; static uint32_t g_len[NFR];

static void make_frames(void){
    for (int i=0;i<NFR;i++){ uint32_t n=300000+(uint32_t)i*300; g_len[i]=n; g_orig[i]=malloc(n);
        for (uint32_t j=0;j<n;j++) g_orig[i][j]=(uint8_t)(i*23+j*11+3); }
}
static int mkimg(const char*p){ int fd=open(p,O_RDWR|O_CREAT|O_TRUNC,0644); if(fd<0)return -1;
    int r=ftruncate(fd,128ull<<20); close(fd); return r; }

int main(int argc,char**argv){
    const char *A=argc>2?argv[1]:"/tmp/rsdk_bA.img", *B=argc>2?argv[2]:"/tmp/rsdk_bB.img";
    printf("RSDK %s 多盘写入均衡 | A=%s B=%s\n", rsdk_version(), A, B);

    rsdk_format_opt_t fo; memset(&fo,0,sizeof fo); fo.chunk_mib=1; fo.sn="NT12-32"; fo.format_time=1784486000;
    if(mkimg(A)||mkimg(B)){perror("mkimg");return 1;}
    if(rsdk_format(A,&fo)||rsdk_format(B,&fo)){printf("format 失败\n");return 1;}

    const char *paths[2]={A,B}; rsdk_group_t *g;
    if(rsdk_group_open(paths,2,&g)!=RSDK_OK){printf("group_open 失败\n");return 1;}

    /* ---- 一个盘组写入器写一路; 每段自动均衡选盘 ---- */
    make_frames();
    rsdk_writer_t *w;
    if(rsdk_rec_open_group(g,13,RSDK_REC_CONTINUOUS,&w)!=RSDK_OK){printf("rec_open_group 失败\n");return 1;}
    for(int i=0;i<NFR;i++){
        rsdk_frame_t f={ .chn=13,.codec=RSDK_CODEC_H265,.frame_type=(i%3==0?RSDK_FRAME_I:RSDK_FRAME_P),
            .pts=(uint64_t)i*3000,.wall_time=1784486200+i,.data=g_orig[i],.len=g_len[i] };
        rsdk_rec_write_frame(w,&f);
    }
    rsdk_rec_close(w);
    printf("写入 %d 帧(一路, 盘组均衡)完成\n", NFR);

    /* ---- 跨盘检索: 看段如何分布到两盘 ---- */
    rsdk_index_slot_t segs[128];
    int ns=rsdk_group_query(g,1784486000,1784486999,13,-1,segs,128);
    int perdisk[2]={0,0};
    printf("\n跨盘检索 %d 段(按时间升序):\n", ns);
    for(int s=0;s<ns;s++){ if(segs[s].start_disk<2) perdisk[segs[s].start_disk]++;
        printf("  #%d 盘%d start_time=%u 帧数=%u\n", s, segs[s].start_disk, segs[s].start_time, segs[s].frame_count); }
    printf("段分布: 盘0=%d 段, 盘1=%d 段  → 一路录像已均摊到两盘 %s\n",
        perdisk[0], perdisk[1], (perdisk[0]>0 && perdisk[1]>0)?"✓":"✗");

    /* ---- 多盘回放 + 逐字节校验 ---- */
    rsdk_group_player_t *gp; rsdk_group_play_open(g,segs,ns,&gp);
    rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len; int disk, gi=0, bad=0, prev=-1, sw=0;
    while(rsdk_group_play_next(gp,&h,&data,&len,&disk)==RSDK_OK){
        if(gi>=NFR){bad++;break;}
        if(len!=g_len[gi]||memcmp(data,g_orig[gi],len)!=0) bad++;
        if(disk!=prev){ sw++; prev=disk; }
        gi++;
    }
    rsdk_group_play_close(gp);
    printf("\n多盘回放: 取回 %d/%d 帧, 跨盘切换 %d 次, 逐字节%s\n",
        gi, NFR, sw-1<0?0:sw-1, bad?"有差异 ✗":"完全一致 ✓");

    rsdk_group_close(g);
    for(int i=0;i<NFR;i++) free(g_orig[i]);
    int ok = (bad==0 && perdisk[0]>0 && perdisk[1]>0);
    printf("\n结果: %s\n", ok?"多盘写入均衡+回放 PASS (一路均摊两盘→跨盘归并回放一致)":"FAIL");
    return ok?0:1;
}
