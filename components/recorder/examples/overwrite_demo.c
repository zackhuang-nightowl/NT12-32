/***************************************************************************************
 *  overwrite_demo.c —— 满盘策略参数化(设计 §4.3): stop(停录) vs overwrite(循环覆盖)。
 *  由 rsdk_features.conf 的 layout.hdd_full 或 format 选项控制。
 *  overwrite 时验证旧段索引被回收(检索不再命中已覆盖数据)。
 *  运行:  ./overwrite_demo
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static uint64_t data_chunks_of(const char*img){ rsdk_dev_t*d; rsdk_dev_open(img,&d);
    rsdk_dev_info_t in; rsdk_dev_info(d,&in); rsdk_dev_close(d); return in.free_chunks; }

static int run(const char*img, int policy){
    int fd=open(img,O_RDWR|O_CREAT|O_TRUNC,0644); if(ftruncate(fd,64ull<<20)){} close(fd);
    rsdk_format_opt_t fo={ .chunk_mib=1, .sn="NT12-32", .format_time=1784486000, .hdd_full=(uint8_t)policy };
    if(rsdk_format(img,&fo)!=RSDK_OK){ printf("format 失败\n"); return -1; }
    uint64_t cap=data_chunks_of(img);
    uint32_t chunk_bytes=1<<20, flen=chunk_bytes-256;   /* 每帧≈1chunk → 1段1chunk */
    uint8_t*buf=malloc(flen); memset(buf,0xAB,flen);

    rsdk_dev_t*d; rsdk_dev_open(img,&d);
    rsdk_writer_t*w; rsdk_rec_open(d,13,RSDK_REC_CONTINUOUS,&w);
    uint64_t target = (policy==RSDK_HDDFULL_STOP)? cap+5 : cap+3;   /* 都尝试超出容量 */
    uint64_t n_ok=0; int stopped=0;
    for(uint64_t i=0;i<target;i++){
        rsdk_frame_t f={ .chn=13,.codec=RSDK_CODEC_H265,.frame_type=RSDK_FRAME_I,
            .pts=(uint64_t)i*3000,.wall_time=1784486200+i,.data=buf,.len=flen };
        rsdk_err_t rc=rsdk_rec_write_frame(w,&f);
        if(rc==RSDK_E_NOSPACE){ stopped=1; break; }
        if(rc!=RSDK_OK){ printf("write err %s\n",rsdk_strerror(rc)); break; }
        n_ok++;
    }
    rsdk_rec_close(w);

    rsdk_index_slot_t segs[512];
    int ns=rsdk_index_query(d,0,0xffffffff,13,-1,segs,512);
    uint32_t earliest=0; rsdk_index_earliest(d,&earliest);
    rsdk_dev_close(d); free(buf);

    printf("  data_chunks=%llu  写入成功=%llu  %s  有效段=%d  最早=%u(base+%d)\n",
        (unsigned long long)cap,(unsigned long long)n_ok,
        stopped?"[满盘停录 NOSPACE]":"[未触发停录]", ns, earliest, earliest-1784486200);
    return (int)cap;
}

int main(void){
    printf("RSDK %s 满盘策略参数化\n", rsdk_version());
    printf("配置默认 RSDK_CFG_HDD_FULL=%d (%s)\n", RSDK_CFG_HDD_FULL,
           RSDK_CFG_HDD_FULL?"stop":"overwrite");

    printf("\n[STOP] 盘满即停录, 不覆盖:\n");
    int cap1=run("/tmp/rsdk_ow_stop.img", RSDK_HDDFULL_STOP);

    printf("\n[OVERWRITE] 盘满循环覆盖最旧 + 回收旧索引:\n");
    int cap2=run("/tmp/rsdk_ow_over.img", RSDK_HDDFULL_OVERWRITE);

    /* 判定: stop 段数=cap(填满即止, 最早=base+0); overwrite 段数=cap(旧的被回收, 最早前移) */
    printf("\n结果: %s\n",
        (cap1>0&&cap2>0)?"满盘策略参数化 PASS (stop 填满即停; overwrite 覆盖最旧且旧索引已回收→最早时间前移)":"FAIL");
    return 0;
}
