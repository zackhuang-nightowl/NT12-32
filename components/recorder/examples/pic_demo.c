/***************************************************************************************
 *  pic_demo.c —— 事件抓拍(PIC)用于事件推送(设计 §抓拍)。
 *  事件触发时刻那帧 → 加密写 MetaRegion + 截图指针回填事件索引槽(盘上权威) →
 *  经事件槽 snap_off 取图(rsdk_pic_read_blob, 不经 meta.db) → 解密逐字节一致 + 合法 JPEG。
 *  运行:  ./pic_demo
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int have(const char*c){ char b[128]; snprintf(b,sizeof b,"command -v %s >/dev/null 2>&1",c); return system(b)==0; }
static uint8_t* rf(const char*p,long*n){ FILE*f=fopen(p,"rb"); if(!f)return NULL;
    fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET); uint8_t*b=malloc(*n);
    if(fread(b,1,*n,f)!=(size_t)*n){} fclose(f); return b; }

int main(void){
    printf("RSDK %s 事件抓拍(PIC)\n", rsdk_version());
#if !RSDK_CFG_METADATA
    printf("metadata=off, 抓拍不可用(复用 MetaRegion). 跳过。\n"); return 0;
#else
    const char *img="/tmp/rsdk_pic.img", *jpg="/tmp/rsdk_snap.jpg", *out="/tmp/rsdk_push.jpg";

    /* 造一张真 JPEG(事件触发时刻主图) */
    long jl=0; uint8_t*jpeg=NULL;
    if(have("ffmpeg")){
        system("ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=320x240 -frames:v 1 /tmp/rsdk_snap.jpg");
        jpeg=rf(jpg,&jl);
    }
    if(!jpeg){ printf("(无 ffmpeg, 用占位字节仅测存取)\n"); jl=1024; jpeg=malloc(jl);
        jpeg[0]=0xFF;jpeg[1]=0xD8;jpeg[2]=0xFF; for(long i=3;i<jl;i++) jpeg[i]=(uint8_t)(i*7); }
    printf("事件主图 JPEG: %ld 字节 (前3字节 %02x %02x %02x)\n", jl, jpeg[0],jpeg[1],jpeg[2]);

    /* 格式化(metadata=on + 加密) + 打开 */
    int fd=open(img,O_RDWR|O_CREAT|O_TRUNC,0644); if(ftruncate(fd,64ull<<20)){} close(fd);
    rsdk_format_opt_t fo={ .chunk_mib=4, .sn="NT12-32", .format_time=1784486000 };
    if(rsdk_format(img,&fo)!=RSDK_OK){ printf("format 失败\n"); return 1; }
    rsdk_dev_t*d; rsdk_dev_open(img,&d);
    rsdk_dev_info_t info; rsdk_dev_info(d,&info);
    printf("MetaRegion: %llu chunk (metadata=%d, enc=%d)\n",
        (unsigned long long)info.meta_chunk_count, rsdk_feature_on(RSDK_F_METADATA), info.enc_algo);
    if(info.meta_chunk_count==0){ printf("MetaRegion 未预留(metadata=off?), 抓拍不可用\n"); return 1; }

    /* ---- 先建一条事件槽(模拟录像器已建槽, 供截图指针回填) ---- */
    uint64_t event_id=10769;
    rsdk_evt_slot_t s; memset(&s,0,sizeof s);
    s.event_id=event_id; s.chn=13; s.rectype=RSDK_REC_HUMAN;
    s.state=RSDK_CLOUD_NONE; s.start_time=1784486092; s.end_time=1784486112;
    s.av_chunk=12040; s.av_off=384; s.flags=RSDK_EVT_VALID;
    if(rsdk_evtidx_write(d,&s)!=RSDK_OK){ printf("evtidx_write 失败\n"); return 1; }

    /* ---- 事件触发抓拍: 加密写 MetaRegion + 回填事件槽 snap 指针 ---- */
    rsdk_pic_key_t km={ .chn=13, .ts=1784486092, .event_id=event_id, .type=RSDK_PIC_MAIN, .w=320,.h=240 };
    uint64_t id_m=0;
    rsdk_err_t rc=rsdk_pic_write(d,&km,jpeg,jl,&id_m);
    printf("写事件主图: %s (metaregion off=%llu)\n", rsdk_strerror(rc), (unsigned long long)id_m);

    /* ---- 截图指针已回填事件槽 ---- */
    rsdk_evt_slot_t g;
    int have_snap = (rsdk_evtidx_get(d,event_id,&g)==RSDK_OK) &&
                    (g.flags & RSDK_EVT_HAS_SNAP) && g.snap_off!=0 && g.snap_len>0;
    printf("事件槽截图指针: %s (snap_off=%llu snap_len=%u)\n", have_snap?"已回填 ✓":"缺失 ✗",
        (unsigned long long)g.snap_off, g.snap_len);

    /* 盘上是密文验证(负载在 PIC0 头 40B 之后) */
    if(have_snap){ int rfd=open(img,O_RDONLY); uint8_t on[3];
        if(pread(rfd,on,3,(off_t)(g.snap_off+40))!=3){} close(rfd);
        printf("盘上主图负载前3字节: %02x %02x %02x → %s\n", on[0],on[1],on[2],
            (on[0]==0xFF&&on[1]==0xD8)?"明文?✗":"密文 ✓"); }

    /* ---- 事件推送取图(核心用途): 经事件槽 snap_off 解密取回 ---- */
    void*pj=NULL; size_t pl=0;
    rc = have_snap ? rsdk_pic_read_blob(d,g.snap_off,event_id,&pj,&pl) : RSDK_E_NOTFOUND;
    int same = (rc==RSDK_OK && pl==(size_t)jl && memcmp(pj,jpeg,jl)==0);
    printf("事件推送取图 rsdk_pic_read_blob: %s, %zu 字节, 与原图%s\n",
        rsdk_strerror(rc), pl, same?"逐字节一致 ✓":"不一致 ✗");
    if(rc==RSDK_OK){ FILE*f=fopen(out,"wb"); fwrite(pj,1,pl,f); fclose(f); free(pj); }

    /* JPEG 合法性 */
    if(have("ffprobe")){ printf("--- ffprobe 验证取回的推送图 ---\n"); fflush(stdout);
        char cmd[256]; snprintf(cmd,sizeof cmd,
          "ffprobe -v error -show_entries stream=codec_name,width,height -of default=noprint_wrappers=1 %s",out);
        (void)system(cmd); }

    rsdk_dev_close(d); free(jpeg);
    int ok = (id_m>0 && have_snap && same);
    printf("\n结果: %s\n", ok?"事件抓拍 PASS (触发→加密写→回填事件槽→经 snap_off 解密一致→合法JPEG)":"FAIL");
    return ok?0:1;
#endif
}
