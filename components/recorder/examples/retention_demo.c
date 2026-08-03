/***************************************************************************************
 *  retention_demo.c —— 覆盖回收生命周期(设计 §12.6): 视频循环覆盖时,
 *  经 rec 覆盖回收回调联动清理绑定到被覆盖 chunk 的元数据/抓拍(retention 一致)。
 *  运行:  ./retention_demo
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#if RSDK_CFG_METADATA
/* 覆盖回收回调: 某视频 chunk 被复用前, 清理绑定它的元数据/抓拍 */
static void on_reclaim(void *meta, uint16_t disk, uint64_t chunk){
    rsdk_meta_purge_chunk(meta, disk, chunk);
}
static int count_all(void *meta){
    rsdk_meta_query_t q; memset(&q,0,sizeof q); q.t0=0; q.t1=0xffffffff; q.chn=-1;
    rsdk_metadoc_list_t l; rsdk_meta_query(meta,&q,&l); int n=l.count; rsdk_meta_free_list(&l); return n;
}
static int has_event(void *meta, uint64_t eid){
    rsdk_meta_query_t q; memset(&q,0,sizeof q); q.t0=0; q.t1=0xffffffff; q.chn=-1; q.event_id=eid;
    rsdk_metadoc_list_t l; rsdk_meta_query(meta,&q,&l); int n=l.count; rsdk_meta_free_list(&l); return n>0;
}
#endif

int main(void){
    printf("RSDK %s 覆盖回收生命周期\n", rsdk_version());
#if !RSDK_CFG_METADATA
    printf("metadata=off, 跳过。\n"); return 0;
#else
    const char *img="/tmp/rsdk_ret.img";
    int fd=open(img,O_RDWR|O_CREAT|O_TRUNC,0644); if(ftruncate(fd,64ull<<20)){} close(fd);
    rsdk_format_opt_t fo={ .chunk_mib=1, .sn="NT12-32", .format_time=1784486000,
        .hdd_full=RSDK_HDDFULL_OVERWRITE };
    if(rsdk_format(img,&fo)!=RSDK_OK){ printf("format 失败\n"); return 1; }
    rsdk_dev_t*d; rsdk_dev_open(img,&d);
    rsdk_dev_info_t info; rsdk_dev_info(d,&info);
    uint64_t cap=info.free_chunks;
    void*meta; rsdk_meta_open(":memory:",&meta);

    rsdk_writer_t*w; rsdk_rec_open(d,13,RSDK_REC_CONTINUOUS,&w);
    rsdk_rec_set_reclaim(w, on_reclaim, meta);       /* 联动清理 */

    uint32_t flen=(1<<20)-256; uint8_t*buf=malloc(flen); memset(buf,0xCD,flen);
    uint8_t jpeg[512]={0xFF,0xD8,0xFF};
    uint64_t total=cap+6, first_eid=0, last_eid=0;
    for(uint64_t i=0;i<total;i++){
        rsdk_frame_t f={ .chn=13,.codec=RSDK_CODEC_H265,.frame_type=RSDK_FRAME_I,
            .pts=(uint64_t)i*3000,.wall_time=1784486200+i,.data=buf,.len=flen };
        if(rsdk_rec_write_frame(w,&f)!=RSDK_OK) break;
        uint64_t cur=rsdk_rec_cur_chunk(w);
        uint64_t eid=1000+i;
        rsdk_meta_key_t mk={ .ts=(uint32_t)(1784486200+i), .chn=13, .event_id=eid,
            .doc_type=RSDK_DOC_AI_EVENT, .seg={.disk=0,.chunk=cur,.off=0,.pts=0} };
        char js[64]; int jl=snprintf(js,sizeof js,"{\"event\":\"human\",\"i\":%llu}",(unsigned long long)i);
        rsdk_meta_put(meta,&mk,js,jl,NULL);
        rsdk_pic_key_t pk={ .chn=13,.ts=(uint32_t)(1784486200+i),.event_id=eid,
            .type=RSDK_PIC_MAIN,.w=320,.h=240,.seg={.disk=0,.chunk=cur} };
        rsdk_pic_write(d,meta,&pk,jpeg,sizeof jpeg,NULL);
        if(i==0) first_eid=eid; last_eid=eid;
    }
    rsdk_rec_close(w); free(buf);

    int rows=count_all(meta);
    int first_gone = !has_event(meta,first_eid);
    int last_here  =  has_event(meta,last_eid);
    uint32_t earliest=0; rsdk_index_earliest(d,&earliest);
    printf("data_chunks=%llu 写入=%llu(超容量6)\n",(unsigned long long)cap,(unsigned long long)total);
    printf("元数据+抓拍 存活行数=%d (期望≈2*容量=%llu, 旧的随覆盖已清理)\n", rows,(unsigned long long)cap*2);
    printf("最旧事件(chunk被覆盖) 已清理: %s\n", first_gone?"是 ✓":"否 ✗");
    printf("最新事件 仍在: %s\n", last_here?"是 ✓":"否 ✗");
    printf("视频最早时间前移: base+%d\n", earliest-1784486200);

    rsdk_meta_close(meta); rsdk_dev_close(d);
    int ok=(first_gone && last_here && rows<=(int)(cap*2)+2 && rows>0);
    printf("\n结果: %s\n", ok?"覆盖回收生命周期 PASS (视频覆盖→绑定的元数据/抓拍同步清理)":"FAIL");
    return ok?0:1;
#endif
}
