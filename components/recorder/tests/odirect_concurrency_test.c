/* odirect_concurrency_test.c — O_DIRECT 聚合写(rsdk_rec)回归测试(主机 + ASAN)
 *
 * 背景: O_DIRECT 聚合写引入了每路可变聚合状态(w->stg / stg_len / stg_off / stg_cap)。
 *       本模块设计不变式 = "唯一线程写 writer"(record worker)。一旦被外来线程并发写同一
 *       writer(历史 bug: stream_record_worker_flush_sync 在调用者线程上驱动 worker_round),
 *       聚合缓冲撕裂 → 堆越界(stage_flush 的 memmove/ memset)→ 崩溃。真机 core 落在 rsdk_rec.c
 *       stage_flush:78 / fill_slot。本测试在主机上把三种访问模式跑通/跑崩, 锁死该不变式:
 *
 *   A       : 单线程写(合法用法)。写小/大/超大(>STAGE_BYTES 触发扩容)帧 + 切段 + flush_due +
 *             datasync, 落盘后重开逐帧回放**逐字节校验**。→ 必须 PASS(证明 O_DIRECT 本身正确)。
 *   Bfixed  : 两线程写同一 writer, 但用互斥串行化。→ 必须 PASS(证明串行化即安全)。
 *   Brace   : 两线程写同一 writer, **无串行**。→ ASAN 必报 heap-buffer-overflow(复现真机根因)。
 *
 * 编译运行见同目录 run.sh。关键: 必须 RSDK_DIO_FORCE_FILE=1 才在常规文件上启用真 O_DIRECT 路径。
 */
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

static uint64_t mono_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000ull + ts.tv_nsec/1000000ull; }

static void fill(uint8_t *b, uint32_t len, uint32_t seed){   /* 确定性内容, 便于回放逐字节校验 */
    b[0]=0;b[1]=0;b[2]=0;b[3]=1;b[4]=0x40;b[5]=0x01;
    for(uint32_t j=6;j<len;j++) b[j]=(uint8_t)(seed*31u + j*7u);
}

/* ---------- A: 单线程正确性 + 回放逐字节一致 ---------- */
#define A_N 400
static uint8_t *g_orig[A_N]; static uint32_t g_len[A_N];

static int test_A(const char *img){
    rsdk_format_opt_t fo; memset(&fo,0,sizeof fo);
    fo.chunk_mib=2; fo.sn="NT12-32"; fo.format_time=1784486000;
    fo.feature_mask=RSDK_FEAT_METADATA;   /* 关加密: 测的是聚合写, 非 crypto(且免 ASAN 下 AES 慢) */
    if(rsdk_format(img,&fo)!=RSDK_OK){ printf("A: format fail\n"); return 1; }
    rsdk_dev_t *d; if(rsdk_dev_open(img,&d)!=RSDK_OK){ printf("A: open fail\n"); return 1; }
    int have_dio = (rsdk_dev_raw_dio(d)!=NULL);

    for(int i=0;i<A_N;i++){
        uint32_t len;
        if(i%37==0)     len = 640*1024 + (uint32_t)i*13;   /* > STAGE_BYTES → 扩容路径 */
        else if(i%3==0) len = 180*1024 + (uint32_t)i*7;
        else            len = 30*1024  + (uint32_t)i*11;
        g_len[i]=len; g_orig[i]=malloc(len); fill(g_orig[i],len,(uint32_t)i);
    }
    rsdk_writer_t *w; rsdk_rec_open(d,5,RSDK_REC_CONTINUOUS,&w);
    uint64_t t0=mono_ms();
    for(int i=0;i<A_N;i++){
        rsdk_frame_t f={.chn=5,.stream=0,.codec=RSDK_CODEC_H265,
            .frame_type=(i%10==0?RSDK_FRAME_I:RSDK_FRAME_P),
            .pts=(uint64_t)i*3000,.wall_time=1784486100+i,.data=g_orig[i],.len=g_len[i]};
        if(rsdk_rec_write_frame(w,&f)!=RSDK_OK){ printf("A: write %d fail\n",i); return 1; }
        if(i%15==0) rsdk_rec_flush_due(w, t0+i*1600ull);   /* 模拟 worker 周期刷(> FLUSH_MS) */
        if(i%53==0 && i) rsdk_rec_datasync(w);
        if(i%40==0 && i && (i%10==0)) rsdk_rec_rotate(w);  /* IDR 处切段 */
    }
    rsdk_rec_close(w);
    rsdk_dev_close(d);

    if(rsdk_dev_open(img,&d)!=RSDK_OK){ printf("A: reopen fail\n"); return 1; }
    rsdk_index_slot_t segs[128];
    int ns=rsdk_index_query(d,1784486000,1784489999,5,-1,segs,128);
    int gi=0,bad=0;
    for(int s=0;s<ns;s++){
        rsdk_player_t *p; rsdk_play_open(d,&segs[s],&p);
        rsdk_frame_hdr_t h; const uint8_t *data; uint32_t len;
        while(rsdk_play_next_frame(p,&h,&data,&len)==RSDK_OK){
            if(gi>=A_N){ bad++; break; }
            if(len!=g_len[gi] || memcmp(data,g_orig[gi],len)!=0) bad++;
            gi++;
        }
        rsdk_play_close(p);
    }
    rsdk_dev_close(d);
    for(int i=0;i<A_N;i++) free(g_orig[i]);
    int ok = (bad==0 && gi==A_N);
    printf("A: segs=%d readback=%d/%d dio=%s → %s\n",
        ns, gi, A_N, have_dio?"O_DIRECT":"buffered", ok?"PASS (byte-exact)":"FAIL");
    return ok?0:1;
}

/* ---------- B: 两线程写同一 writer ---------- */
static rsdk_writer_t *gB_w;
static int gB_serialize;
static pthread_mutex_t gB_mtx = PTHREAD_MUTEX_INITIALIZER;
#define B_PER_THREAD 3000

static void *b_thread(void *arg){
    long tid=(long)arg;
    uint8_t *buf=malloc(900*1024);
    for(int i=0;i<B_PER_THREAD;i++){
        uint32_t len = (i%29==0)? 600*1024+(uint32_t)i     /* 触发刷+扩容 */
                     : (i%2==0)?  120*1024+(uint32_t)i
                     :            20*1024 +(uint32_t)i;
        fill(buf,len,(uint32_t)(tid*100000+i));
        rsdk_frame_t f={.chn=7,.stream=0,.codec=RSDK_CODEC_H265,
            .frame_type=(i%8==0?RSDK_FRAME_I:RSDK_FRAME_P),
            .pts=(uint64_t)i*3000,.wall_time=1784486100+i,.data=buf,.len=len};
        if(gB_serialize) pthread_mutex_lock(&gB_mtx);
        rsdk_rec_write_frame(gB_w,&f);
        if(gB_serialize) pthread_mutex_unlock(&gB_mtx);
    }
    free(buf);
    return NULL;
}

static int test_B(const char *img,int serialize){
    gB_serialize=serialize;
    rsdk_format_opt_t fo; memset(&fo,0,sizeof fo);
    fo.chunk_mib=2; fo.sn="NT12-32"; fo.format_time=1784486000; fo.feature_mask=RSDK_FEAT_METADATA;
    if(rsdk_format(img,&fo)!=RSDK_OK){ printf("B: format fail\n"); return 1; }
    rsdk_dev_t *d; if(rsdk_dev_open(img,&d)!=RSDK_OK){ printf("B: open fail\n"); return 1; }
    rsdk_rec_open(d,7,RSDK_REC_CONTINUOUS,&gB_w);
    pthread_t t1,t2;
    pthread_create(&t1,NULL,b_thread,(void*)1);
    pthread_create(&t2,NULL,b_thread,(void*)2);
    pthread_join(t1,NULL); pthread_join(t2,NULL);
    rsdk_rec_close(gB_w);
    rsdk_dev_close(d);
    printf("B(%s): 完成, 无 ASAN 中止%s\n", serialize?"serialized":"RACE",
           serialize?" → PASS":" (若到这里说明本次未触发, 多跑几次)");
    return 0;
}

/* ---------- Shard: 一盘一写运行模型的安全性(N 线程各写不相交 writer 集 + 共享盘组) ----------
 * 复刻固件 per-disk sharding: 2 盘组, 8 通道×2 流 = 16 个 writer; 线程 k 只写 chn%2==k 的通道(不相交)。
 * 并发触发: 同组多线程 start_seg→balance_pick(组锁)、不同盘 index/evtidx 写、切段、事件标记。
 * 期望: ASAN 干净(每个 writer 仅被其归属线程写 = 单线程写单 writer)。 */
#define S_NCH 8
#define S_NSHARD 2
#define S_PER 700
static rsdk_group_t *gS;
static rsdk_writer_t *gS_wr[S_NCH][2];

static void *shard_thread(void *arg){
    int k=(int)(long)arg;
    uint8_t *buf=malloc(700*1024);
    for(int rep=0; rep<S_PER; rep++){
        for(int chn=0; chn<S_NCH; chn++){
            if((chn % S_NSHARD) != k) continue;        /* shard k 只碰归属通道(不相交) */
            for(int st=0; st<2; st++){
                rsdk_writer_t *w=gS_wr[chn][st];
                if(!w) continue;
                uint32_t len = (rep%29==0)? 600*1024+(uint32_t)rep
                             : (rep%2)?     20*1024 +(uint32_t)rep
                             :              100*1024+(uint32_t)rep;
                fill(buf,len,(uint32_t)(chn*100000+rep));
                rsdk_frame_t f={.chn=(uint16_t)chn,.stream=(uint8_t)st,.codec=RSDK_CODEC_H265,
                    .frame_type=(rep%8==0?RSDK_FRAME_I:RSDK_FRAME_P),
                    .pts=(uint64_t)rep*3000,.wall_time=1784486100+rep,.data=buf,.len=len};
                rsdk_rec_write_frame(w,&f);
                if(rep%40==0 && rep && rep%8==0) rsdk_rec_rotate(w);
                if(st==0 && rep%50==0 && rep)
                    rsdk_rec_mark_event(w, 1000+chn, RSDK_REC_MOTION, 1784486100+rep, 0, 1u, 0);
            }
        }
    }
    free(buf);
    return NULL;
}

static int test_Shard(const char *imgA, const char *imgB){
    for(const char **p=(const char*[]){imgA,imgB,0}; *p; p++){
        int fd=open(*p,O_RDWR|O_CREAT|O_TRUNC,0644);
        if(fd<0||ftruncate(fd,128ull<<20)!=0){perror("mkimg");return 1;} close(fd);
    }
    rsdk_format_opt_t fo; memset(&fo,0,sizeof fo);
    fo.chunk_mib=2; fo.sn="NT12-32"; fo.format_time=1784486000; fo.feature_mask=RSDK_FEAT_METADATA;
    if(rsdk_format(imgA,&fo)||rsdk_format(imgB,&fo)){ printf("Shard: format fail\n"); return 1; }
    const char *paths[2]={imgA,imgB};
    if(rsdk_group_open(paths,2,&gS)!=RSDK_OK){ printf("Shard: group_open fail\n"); return 1; }
    printf("Shard: 盘组=%d 盘, %d 线程 × %d 通道(不相交), dio_disk0=%p\n",
        rsdk_group_count(gS), S_NSHARD, S_NCH, (void*)rsdk_dev_raw_dio(rsdk_group_dev(gS,0)));
    for(int chn=0;chn<S_NCH;chn++) for(int st=0;st<2;st++)
        if(rsdk_rec_open_group_stream(gS,chn,RSDK_REC_CONTINUOUS,st,&gS_wr[chn][st])!=RSDK_OK)
            gS_wr[chn][st]=NULL;
    pthread_t th[S_NSHARD];
    for(long k=0;k<S_NSHARD;k++) pthread_create(&th[k],NULL,shard_thread,(void*)k);
    for(int k=0;k<S_NSHARD;k++) pthread_join(th[k],NULL);
    for(int chn=0;chn<S_NCH;chn++) for(int st=0;st<2;st++)
        if(gS_wr[chn][st]) rsdk_rec_close(gS_wr[chn][st]);
    rsdk_group_close(gS);
    printf("Shard: 完成, 无 ASAN 中止 → PASS(不相交 writer 并发 + 共享盘组安全)\n");
    return 0;
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IOLBF,0);
    const char *mode = argc>1?argv[1]:"A";
    const char *img  = argc>2?argv[2]:"/tmp/odirect_test.img";
    int fd=open(img,O_RDWR|O_CREAT|O_TRUNC,0644);
    if(fd<0){perror("open");return 1;}
    uint64_t imb = getenv("IMG_MB")?(uint64_t)atoi(getenv("IMG_MB")):128;   /* 磁盘紧张可调小(≥64) */
    if(ftruncate(fd,imb<<20)!=0){perror("ftruncate");return 1;}
    close(fd);
    printf("RSDK %s | ODIRECT_env=%s | mode=%s\n", rsdk_version(),
           getenv("RSDK_DIO_FORCE_FILE")?"ON":"off(设 RSDK_DIO_FORCE_FILE=1 才测真 O_DIRECT)", mode);
    if(!strcmp(mode,"A"))      return test_A(img);
    if(!strcmp(mode,"Bfixed")) return test_B(img,1);
    if(!strcmp(mode,"Brace"))  return test_B(img,0);
    if(!strcmp(mode,"Shard")){ char b[512]; snprintf(b,sizeof b,"%s.b",img); return test_Shard(img,b); }
    printf("unknown mode (A|Bfixed|Brace|Shard)\n"); return 2;
}
