/***************************************************************************************
 *  backup_demo.c —— 录像导出 MP4(设计 §7.6)。
 *  若有 ffmpeg: 造真 HEVC Annex-B → 拆访问单元写盘(加密) → rsdk_backup_export 出 MP4
 *              → ffprobe/ffmpeg 验证可解码。否则跳过(需真码流)。
 *
 *  运行:  ./backup_demo
 ***************************************************************************************/
#include "rsdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int have(const char*c){ char cmd[128]; snprintf(cmd,sizeof cmd,"command -v %s >/dev/null 2>&1",c); return system(cmd)==0; }

/* 读整个文件 */
static uint8_t* readfile(const char*p, long*n){
    FILE*f=fopen(p,"rb"); if(!f) return NULL; fseek(f,0,SEEK_END); *n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(*n); if(fread(b,1,*n,f)!=(size_t)*n){} fclose(f); return b;
}

/* 把 HEVC Annex-B 拆成访问单元, 逐个作为一帧写入 rec */
static int split_and_write(rsdk_writer_t*w, const uint8_t*d, long len){
    uint8_t*au=malloc(len+16); long al=0; int has_vcl=0,key=0,idx=0,frames=0;
    long i=0;
    #define FLUSH() do{ if(al>0){ rsdk_frame_t f={.chn=13,.codec=RSDK_CODEC_H265, \
        .frame_type=key?RSDK_FRAME_I:RSDK_FRAME_P,.pts=(uint64_t)idx*6000,.wall_time=1784486300+idx, \
        .data=au,.len=(uint32_t)al}; rsdk_rec_write_frame(w,&f); idx++; frames++; al=0; has_vcl=0; key=0; } }while(0)
    while(i+3<=len){
        if(d[i]==0&&d[i+1]==0&&d[i+2]==1){
            long s=i+3, j=s;
            while(j+3<=len && !(d[j]==0&&d[j+1]==0&&d[j+2]==1)) j++;
            long e=(j+3<=len)?j:len; while(e>s&&d[e-1]==0) e--;
            if(e>s){
                int t=(d[s]>>1)&0x3f, vcl=(t<=31);
                int first=vcl && e>s+2 && (d[s+2]>>7)&1;
                if(vcl && first && has_vcl) FLUSH();
                au[al++]=0;au[al++]=0;au[al++]=0;au[al++]=1;
                memcpy(au+al,d+s,e-s); al+=e-s;
                if(vcl) has_vcl=1;
                if(t>=16&&t<=21) key=1;
            }
            i=(j+3<=len)?j:len;
        } else i++;
    }
    FLUSH();
    free(au);
    return frames;
}

int main(void){
    printf("RSDK %s 导出 MP4\n", rsdk_version());
    const char *h265="/tmp/rsdk_src.h265", *mp4="/tmp/rsdk_out.mp4", *img="/tmp/rsdk_bk.img";

    /* 导出格式可扩展性: 请求未实现格式应被拒 */
    rsdk_export_opt_t bad={ .fmt=RSDK_EXPORT__MAX };
    printf("扩展性检查: 请求未实现格式 → %s (应 bad param)\n",
           rsdk_strerror(rsdk_backup_export(NULL,0,0,0,&bad,mp4)));

    if(!have("ffmpeg")){ printf("(无 ffmpeg, 跳过真码流导出验证)\n"); return 0; }

    /* 造 2 秒 640x360 15fps HEVC Annex-B, 每 15 帧一个 IDR(带参数集) */
    char cmd[512];
    snprintf(cmd,sizeof cmd,
      "ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=640x360:rate=15 -t 2 "
      "-c:v libx265 -x265-params keyint=15:min-keyint=15:log-level=none -f hevc %s", h265);
    if(system(cmd)!=0){ printf("ffmpeg 生成失败\n"); return 1; }
    long slen=0; uint8_t*src=readfile(h265,&slen);
    printf("生成 HEVC 源: %ld 字节\n", slen);

    /* 格式化(加密) + 写盘 */
    int fd=open(img,O_RDWR|O_CREAT|O_TRUNC,0644); if(ftruncate(fd,64ull<<20)){} close(fd);
    rsdk_format_opt_t fo={ .chunk_mib=4, .sn="NT12-32", .format_time=1784486000 };
    rsdk_format(img,&fo);
    rsdk_dev_t*d; rsdk_dev_open(img,&d);
    rsdk_writer_t*w; rsdk_rec_open(d,13,RSDK_REC_CONTINUOUS,&w);
    int nf=split_and_write(w,src,slen);
    rsdk_rec_close(w);
    free(src);
    printf("拆出访问单元并加密写盘: %d 帧\n", nf);

    /* 导出为 MP4(解密 + 封装) */
    rsdk_export_opt_t eo={ .fmt=RSDK_EXPORT_MP4, .width=640, .height=360, .timescale=90000, .fps=15 };
    rsdk_index_slot_t segs[64];
    int ns=rsdk_index_query(d,1784486000,1784486999,13,-1,segs,64);
    rsdk_err_t rc=RSDK_E_NOTFOUND;
    /* 单盘: 逐段导出到同一 MP4? 这里演示导出第一段; 全段导出用 rsdk_backup_export(盘组) */
    if(ns>0) rc=rsdk_backup_export_seg(d,&segs[0],&eo,mp4);
    rsdk_dev_close(d);
    printf("导出 rsdk_backup_export_seg → %s : %s\n", mp4, rsdk_strerror(rc));

    /* 验证: ffprobe 读容器 + ffmpeg 解一帧 */
    printf("\n--- ffprobe 验证导出的 MP4 ---\n"); fflush(stdout);
    snprintf(cmd,sizeof cmd,
      "ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,nb_read_frames "
      "-count_frames -of default=noprint_wrappers=1 %s", mp4);
    if(system(cmd)){}
    printf("--- ffmpeg 解首帧为 PNG(证明可解码) ---\n"); fflush(stdout);
    snprintf(cmd,sizeof cmd,"ffmpeg -y -loglevel error -i %s -frames:v 1 /tmp/rsdk_out_frame.png && ls -l /tmp/rsdk_out_frame.png", mp4);
    int dr=system(cmd);
    /* ---- 扩展格式: 导出 fragmented MP4(同一 muxer 框架) ---- */
    const char *fmp4="/tmp/rsdk_out.fmp4.mp4";
    rsdk_export_opt_t fo2=eo; fo2.fmt=RSDK_EXPORT_FMP4;
    rsdk_dev_open(img,&d);
    rsdk_err_t frc = (ns>0)? rsdk_backup_export_seg(d,&segs[0],&fo2,fmp4) : RSDK_E_NOTFOUND;
    rsdk_dev_close(d);
    printf("\n导出 fMP4(RSDK_EXPORT_FMP4) → %s : %s\n", fmp4, rsdk_strerror(frc));
    printf("--- ffprobe 验证 fMP4 ---\n"); fflush(stdout);
    snprintf(cmd,sizeof cmd,
      "ffprobe -v error -select_streams v:0 -count_frames -show_entries stream=codec_name,nb_read_frames "
      "-of default=noprint_wrappers=1 %s", fmp4);
    int fp=system(cmd);

    int ok=(rc==RSDK_OK && dr==0 && frc==RSDK_OK && fp==0);
    printf("\n结果: %s\n", ok?"导出 PASS (MP4 + fMP4 均标准可解码; muxer 框架可扩展)":"FAIL");
    return ok?0:1;
}
