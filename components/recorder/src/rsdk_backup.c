/* Copyright (C) 2025-2026, Nightowl DG. RSDK 录像导出(设计 §7.6).
 * 扩展框架(muxer 注册) + MP4 muxer(H.265 hvc1 / H.264 avc1)。
 * 从(跨盘、已解密的)Annex-B 帧封装为标准 MP4(moov 在尾, stco 绝对偏移)。 */
#include "rsdk_backup.h"
#include "rsdk_play.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 动态缓冲 + box 构建(用于 moov) ================= */
typedef struct { uint8_t *p; size_t len, cap; } buf_t;
static void bneed(buf_t *b, size_t n){ if(b->len+n>b->cap){ b->cap=(b->len+n)*2+256; b->p=realloc(b->p,b->cap);} }
static void b8 (buf_t *b,uint8_t v){ bneed(b,1); b->p[b->len++]=v; }
static void b16(buf_t *b,uint32_t v){ b8(b,v>>8); b8(b,v); }
static void b24(buf_t *b,uint32_t v){ b8(b,v>>16); b8(b,v>>8); b8(b,v); }
static void b32(buf_t *b,uint32_t v){ b8(b,v>>24); b8(b,v>>16); b8(b,v>>8); b8(b,v); }
static void bmem(buf_t *b,const void*d,size_t n){ bneed(b,n); memcpy(b->p+b->len,d,n); b->len+=n; }
static size_t box_begin(buf_t *b,const char*t){ size_t pos=b->len; b32(b,0); bmem(b,t,4); return pos; }
static void   box_end(buf_t *b,size_t pos){ uint32_t sz=(uint32_t)(b->len-pos);
    b->p[pos]=sz>>24; b->p[pos+1]=sz>>16; b->p[pos+2]=sz>>8; b->p[pos+3]=sz; }

/* ================= MP4 muxer ================= */
typedef struct { uint32_t off,size,dur,key; uint64_t pts; } samp_t;
typedef struct {
    FILE *f; long mdat_pos; int codec;
    uint32_t w,h,ts,fps;
    uint8_t vps[512],sps[512],pps[512]; int vlen,slen,plen;
    samp_t *s; int n,cap;
} mp4_t;

/* Annex-B → 遍历 NAL(去起始码, 去尾部0) */
static void for_each_nal(const uint8_t*d,uint32_t len,
                         void(*cb)(void*,const uint8_t*,uint32_t),void*u){
    uint32_t i=0;
    while(i+3<=len){
        if(d[i]==0&&d[i+1]==0&&d[i+2]==1){
            uint32_t s=i+3, j=s;
            while(j+3<=len && !(d[j]==0&&d[j+1]==0&&d[j+2]==1)) j++;
            uint32_t e=(j+3<=len)?j:len;
            while(e>s && d[e-1]==0) e--;          /* 去尾0(下一起始码前导) */
            if(e>s) cb(u,d+s,e-s);
            i=(j+3<=len)?j:len;
        } else i++;
    }
}
static int is_vcl(int codec,int t){ return codec==RSDK_CODEC_H265 ? (t>=0&&t<=31) : (t>=1&&t<=5); }
static int is_ps (int codec,int t){ return codec==RSDK_CODEC_H265 ? (t==32||t==33||t==34) : (t==7||t==8); }
static int is_key(int codec,int t){ return codec==RSDK_CODEC_H265 ? (t>=16&&t<=21) : (t==5); }

typedef struct { mp4_t*m; uint32_t off; uint32_t size; int key; int started; } auctx_t;
static void nal_cb(void*u,const uint8_t*nal,uint32_t nl){
    auctx_t*a=u; mp4_t*m=a->m;
    int t = m->codec==RSDK_CODEC_H265 ? (nal[0]>>1)&0x3f : nal[0]&0x1f;
    if(is_ps(m->codec,t)){                         /* 参数集: 存进配置记录, 不入 mdat */
        if(m->codec==RSDK_CODEC_H265){
            if(t==32&&nl<sizeof m->vps){ memcpy(m->vps,nal,nl); m->vlen=nl; }
            if(t==33&&nl<sizeof m->sps){ memcpy(m->sps,nal,nl); m->slen=nl; }
            if(t==34&&nl<sizeof m->pps){ memcpy(m->pps,nal,nl); m->plen=nl; }
        } else {
            if(t==7&&nl<sizeof m->sps){ memcpy(m->sps,nal,nl); m->slen=nl; }
            if(t==8&&nl<sizeof m->pps){ memcpy(m->pps,nal,nl); m->plen=nl; }
        }
        return;
    }
    if(!is_vcl(m->codec,t)) return;                /* SEI/AUD 等丢弃 */
    if(!a->started){ a->off=(uint32_t)ftell(m->f); a->started=1; }
    uint8_t L[4]={nl>>24,nl>>16,nl>>8,nl};         /* 4字节长度前缀(AVCC/HVCC) */
    fwrite(L,1,4,m->f); fwrite(nal,1,nl,m->f);
    a->size += 4+nl;
    if(is_key(m->codec,t)) a->key=1;
}

static void mp4_ftyp(FILE*f){
    buf_t b={0}; size_t p=box_begin(&b,"ftyp");
    bmem(&b,"isom",4); b32(&b,0x200); bmem(&b,"isom",4); bmem(&b,"iso2",4);
    bmem(&b,"mp41",4); bmem(&b,"hvc1",4); box_end(&b,p);
    fwrite(b.p,1,b.len,f); free(b.p);
}

static void*mp4_open(const char*path,const rsdk_export_opt_t*opt){
    mp4_t*m=calloc(1,sizeof*m); if(!m) return NULL;
    m->f=fopen(path,"wb"); if(!m->f){ free(m); return NULL; }
    m->w=opt&&opt->width?opt->width:3840; m->h=opt&&opt->height?opt->height:2160;
    m->ts=opt&&opt->timescale?opt->timescale:90000; m->fps=opt&&opt->fps?opt->fps:15;
    m->codec=-1;
    mp4_ftyp(m->f);
    m->mdat_pos=ftell(m->f);
    { uint8_t hdr[8]={0,0,0,0,'m','d','a','t'}; fwrite(hdr,1,8,m->f); } /* size 尾部回填 */
    return m;
}

static int mp4_add(void*ctx,const uint8_t*annexb,uint32_t len,uint64_t pts,int key,int codec){
    mp4_t*m=ctx; if(m->codec<0) m->codec=codec;
    auctx_t a={.m=m}; for_each_nal(annexb,len,nal_cb,&a);
    if(!a.started) return 0;                       /* 该帧无 VCL(纯参数集), 跳过 */
    if(m->n==m->cap){ m->cap=m->cap?m->cap*2:256; m->s=realloc(m->s,m->cap*sizeof(samp_t)); }
    m->s[m->n].off=a.off; m->s[m->n].size=a.size; m->s[m->n].pts=pts;
    m->s[m->n].key=a.key||key; m->s[m->n].dur=0; m->n++;
    return 0;
}

/* ---- 配置记录 ---- */
typedef struct { int codec; const uint8_t*vps,*sps,*pps; int vlen,slen,plen; uint32_t w,h; } cfg_t;

static void put_avcC(buf_t*b,const cfg_t*c){
    size_t p=box_begin(b,"avcC");
    b8(b,1); b8(b,c->sps[1]); b8(b,c->sps[2]); b8(b,c->sps[3]);
    b8(b,0xFF); b8(b,0xE1); b16(b,c->slen); bmem(b,c->sps,c->slen);
    b8(b,0x01); b16(b,c->plen); bmem(b,c->pps,c->plen);
    box_end(b,p);
}
static void put_hvcC(buf_t*b,const cfg_t*c){
    size_t p=box_begin(b,"hvcC"); const uint8_t*s=c->sps;
    b8(b,1); b8(b,s[3]);
    b8(b,s[4]);b8(b,s[5]);b8(b,s[6]);b8(b,s[7]);
    b8(b,s[8]);b8(b,s[9]);b8(b,s[10]);b8(b,s[11]);b8(b,s[12]);b8(b,s[13]);
    b8(b,s[14]); b16(b,0xF000); b8(b,0xFC); b8(b,0xFC|1); b8(b,0xF8); b8(b,0xF8);
    b16(b,0); b8(b,0x03); b8(b,3);
    b8(b,0x80|32); b16(b,1); b16(b,c->vlen); bmem(b,c->vps,c->vlen);
    b8(b,0x80|33); b16(b,1); b16(b,c->slen); bmem(b,c->sps,c->slen);
    b8(b,0x80|34); b16(b,1); b16(b,c->plen); bmem(b,c->pps,c->plen);
    box_end(b,p);
}
/* stsd 内视觉样本条目(hvc1/avc1 + 配置记录), mp4/fmp4 共用 */
static void put_sample_entry(buf_t*b,const cfg_t*c){
    const char*type=c->codec==RSDK_CODEC_H265?"hvc1":"avc1";
    size_t se=box_begin(b,type);
    for(int i=0;i<6;i++) b8(b,0); b16(b,1);
    b16(b,0); b16(b,0); b32(b,0);b32(b,0);b32(b,0);
    b16(b,c->w); b16(b,c->h);
    b32(b,0x00480000); b32(b,0x00480000); b32(b,0); b16(b,1);
    for(int i=0;i<32;i++) b8(b,0);
    b16(b,0x0018); b16(b,0xFFFF);
    if(c->codec==RSDK_CODEC_H265) put_hvcC(b,c); else put_avcC(b,c);
    box_end(b,se);
}
static void cfg_from_mp4(cfg_t*c,mp4_t*m){ c->codec=m->codec; c->vps=m->vps;c->sps=m->sps;c->pps=m->pps;
    c->vlen=m->vlen;c->slen=m->slen;c->plen=m->plen; c->w=m->w;c->h=m->h; }

static void put_stbl(buf_t*b,mp4_t*m){
    size_t stbl=box_begin(b,"stbl");
    size_t stsd=box_begin(b,"stsd"); b32(b,0); b32(b,1);
    cfg_t c; cfg_from_mp4(&c,m); put_sample_entry(b,&c);
    box_end(b,stsd);
    /* stts(压缩相等 delta) */
    size_t stts=box_begin(b,"stts"); b32(b,0);
    size_t cntpos=b->len; b32(b,0); uint32_t runs=0,i=0;
    while((int)i<m->n){ uint32_t d=m->s[i].dur,c=1;
        while((int)(i+c)<m->n && m->s[i+c].dur==d) c++;
        b32(b,c); b32(b,d); runs++; i+=c; }
    b->p[cntpos]=runs>>24;b->p[cntpos+1]=runs>>16;b->p[cntpos+2]=runs>>8;b->p[cntpos+3]=runs;
    box_end(b,stts);
    /* stss(关键帧) */
    size_t stss=box_begin(b,"stss"); b32(b,0);
    size_t kp=b->len; b32(b,0); uint32_t nk=0;
    for(int j=0;j<m->n;j++) if(m->s[j].key){ b32(b,j+1); nk++; }
    b->p[kp]=nk>>24;b->p[kp+1]=nk>>16;b->p[kp+2]=nk>>8;b->p[kp+3]=nk;
    box_end(b,stss);
    /* stsc: 单 chunk 含全部样本 */
    size_t stsc=box_begin(b,"stsc"); b32(b,0); b32(b,1); b32(b,1); b32(b,(uint32_t)m->n); b32(b,1); box_end(b,stsc);
    /* stsz */
    size_t stsz=box_begin(b,"stsz"); b32(b,0); b32(b,0); b32(b,(uint32_t)m->n);
    for(int j=0;j<m->n;j++) b32(b,m->s[j].size); box_end(b,stsz);
    /* stco: 1 chunk, 偏移=首样本文件偏移 */
    size_t stco=box_begin(b,"stco"); b32(b,0); b32(b,1); b32(b,m->n?m->s[0].off:0); box_end(b,stco);
    box_end(b,stbl);
}

static const uint8_t IDENTITY[36]={0,1,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,1,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0x40,0,0,0};

static int mp4_finish(void*ctx){
    mp4_t*m=ctx; int rc=0;
    /* 回填 mdat 大小 */
    long end=ftell(m->f); uint32_t msz=(uint32_t)(end-m->mdat_pos);
    fseek(m->f,m->mdat_pos,SEEK_SET);
    { uint8_t sz[4]={msz>>24,msz>>16,msz>>8,msz}; fwrite(sz,1,4,m->f); }
    fseek(m->f,end,SEEK_SET);
    /* 计算时长 */
    for(int i=0;i<m->n;i++){
        uint32_t d;
        if(i+1<m->n && m->s[i+1].pts>m->s[i].pts) d=(uint32_t)(m->s[i+1].pts-m->s[i].pts);
        else if(i>0) d=m->s[i-1].dur; else d=m->ts/(m->fps?m->fps:15);
        if(d==0) d=m->ts/(m->fps?m->fps:15);
        m->s[i].dur=d;
    }
    uint64_t total=0; for(int i=0;i<m->n;i++) total+=m->s[i].dur;
    uint32_t movie_ts=1000; uint32_t movie_dur=(uint32_t)(total*movie_ts/(m->ts?m->ts:1));

    buf_t b={0};
    size_t moov=box_begin(&b,"moov");
    /* mvhd */
    size_t mvhd=box_begin(&b,"mvhd"); b32(&b,0); b32(&b,0);b32(&b,0);
    b32(&b,movie_ts); b32(&b,movie_dur); b32(&b,0x00010000); b16(&b,0x0100); b16(&b,0); b32(&b,0);b32(&b,0);
    bmem(&b,IDENTITY,36); for(int i=0;i<6;i++) b32(&b,0); b32(&b,2); box_end(&b,mvhd);
    /* trak */
    size_t trak=box_begin(&b,"trak");
    size_t tkhd=box_begin(&b,"tkhd"); b32(&b,7); b32(&b,0);b32(&b,0); b32(&b,1); b32(&b,0); b32(&b,movie_dur);
    b32(&b,0);b32(&b,0); b16(&b,0);b16(&b,0); b16(&b,0);b16(&b,0); bmem(&b,IDENTITY,36);
    b32(&b,m->w<<16); b32(&b,m->h<<16); box_end(&b,tkhd);
    size_t mdia=box_begin(&b,"mdia");
    size_t mdhd=box_begin(&b,"mdhd"); b32(&b,0); b32(&b,0);b32(&b,0); b32(&b,m->ts); b32(&b,(uint32_t)total);
    b16(&b,0x55C4); b16(&b,0); box_end(&b,mdhd);
    size_t hdlr=box_begin(&b,"hdlr"); b32(&b,0); b32(&b,0); bmem(&b,"vide",4); b32(&b,0);b32(&b,0);b32(&b,0);
    bmem(&b,"VideoHandler",12); b8(&b,0); box_end(&b,hdlr);
    size_t minf=box_begin(&b,"minf");
    size_t vmhd=box_begin(&b,"vmhd"); b32(&b,1); b16(&b,0); b16(&b,0);b16(&b,0);b16(&b,0); box_end(&b,vmhd);
    size_t dinf=box_begin(&b,"dinf"); size_t dref=box_begin(&b,"dref"); b32(&b,0); b32(&b,1);
    size_t url=box_begin(&b,"url "); b32(&b,1); box_end(&b,url); box_end(&b,dref); box_end(&b,dinf);
    put_stbl(&b,m);
    box_end(&b,minf); box_end(&b,mdia); box_end(&b,trak);
    box_end(&b,moov);
    if(fwrite(b.p,1,b.len,m->f)!=b.len) rc=-1;
    free(b.p);
    fclose(m->f); free(m->s); free(m);
    return rc;
}

static const rsdk_muxer_t MP4_MUXER = { "mp4", RSDK_EXPORT_MP4, mp4_open, mp4_add, mp4_finish };

/* ================= fragmented MP4 muxer(扩展示例) ================= */
typedef struct { uint32_t size,dur,key; uint64_t pts; } fsamp_t;
typedef struct {
    FILE*f; int codec,init; uint32_t w,h,ts,fps,seq; uint64_t base_dt;
    uint8_t vps[512],sps[512],pps[512]; int vlen,slen,plen;
    buf_t frag; fsamp_t *fs; int fn,fcap;
} fmp4_t;

static void fmp4_ftyp(FILE*f){
    buf_t b={0}; size_t p=box_begin(&b,"ftyp");
    bmem(&b,"iso5",4); b32(&b,0x200); bmem(&b,"iso5",4); bmem(&b,"iso6",4);
    bmem(&b,"mp41",4); bmem(&b,"dash",4); box_end(&b,p);
    fwrite(b.p,1,b.len,f); free(b.p);
}
static void fmp4_write_init(fmp4_t*m){
    buf_t b={0};
    size_t moov=box_begin(&b,"moov");
    size_t mvhd=box_begin(&b,"mvhd"); b32(&b,0);b32(&b,0);b32(&b,0); b32(&b,1000); b32(&b,0);
    b32(&b,0x00010000); b16(&b,0x0100); b16(&b,0); b32(&b,0);b32(&b,0); bmem(&b,IDENTITY,36);
    for(int i=0;i<6;i++) b32(&b,0); b32(&b,2); box_end(&b,mvhd);
    size_t trak=box_begin(&b,"trak");
    size_t tkhd=box_begin(&b,"tkhd"); b32(&b,7); b32(&b,0);b32(&b,0); b32(&b,1); b32(&b,0); b32(&b,0);
    b32(&b,0);b32(&b,0); b16(&b,0);b16(&b,0); b16(&b,0);b16(&b,0); bmem(&b,IDENTITY,36);
    b32(&b,m->w<<16); b32(&b,m->h<<16); box_end(&b,tkhd);
    size_t mdia=box_begin(&b,"mdia");
    size_t mdhd=box_begin(&b,"mdhd"); b32(&b,0); b32(&b,0);b32(&b,0); b32(&b,m->ts); b32(&b,0);
    b16(&b,0x55C4); b16(&b,0); box_end(&b,mdhd);
    size_t hdlr=box_begin(&b,"hdlr"); b32(&b,0); b32(&b,0); bmem(&b,"vide",4); b32(&b,0);b32(&b,0);b32(&b,0);
    bmem(&b,"VideoHandler",12); b8(&b,0); box_end(&b,hdlr);
    size_t minf=box_begin(&b,"minf");
    size_t vmhd=box_begin(&b,"vmhd"); b32(&b,1); b16(&b,0);b16(&b,0);b16(&b,0);b16(&b,0); box_end(&b,vmhd);
    size_t dinf=box_begin(&b,"dinf"); size_t dref=box_begin(&b,"dref"); b32(&b,0); b32(&b,1);
    size_t url=box_begin(&b,"url "); b32(&b,1); box_end(&b,url); box_end(&b,dref); box_end(&b,dinf);
    size_t stbl=box_begin(&b,"stbl");
    size_t stsd=box_begin(&b,"stsd"); b32(&b,0); b32(&b,1);
    cfg_t c={.codec=m->codec,.vps=m->vps,.sps=m->sps,.pps=m->pps,.vlen=m->vlen,.slen=m->slen,.plen=m->plen,.w=m->w,.h=m->h};
    put_sample_entry(&b,&c); box_end(&b,stsd);
    size_t x;
    x=box_begin(&b,"stts"); b32(&b,0); b32(&b,0); box_end(&b,x);
    x=box_begin(&b,"stsc"); b32(&b,0); b32(&b,0); box_end(&b,x);
    x=box_begin(&b,"stsz"); b32(&b,0); b32(&b,0); b32(&b,0); box_end(&b,x);
    x=box_begin(&b,"stco"); b32(&b,0); b32(&b,0); box_end(&b,x);
    box_end(&b,stbl); box_end(&b,minf); box_end(&b,mdia); box_end(&b,trak);
    size_t mvex=box_begin(&b,"mvex");
    size_t trex=box_begin(&b,"trex"); b32(&b,0); b32(&b,1); b32(&b,1); b32(&b,0); b32(&b,0); b32(&b,0);
    box_end(&b,trex); box_end(&b,mvex);
    box_end(&b,moov);
    fwrite(b.p,1,b.len,m->f); free(b.p); m->init=1;
}
static void fmp4_flush(fmp4_t*m){
    if(m->fn==0) return;
    for(int i=0;i<m->fn;i++){ uint32_t d;
        if(i+1<m->fn && m->fs[i+1].pts>m->fs[i].pts) d=(uint32_t)(m->fs[i+1].pts-m->fs[i].pts);
        else d=m->ts/(m->fps?m->fps:15);
        m->fs[i].dur=d?d:m->ts/(m->fps?m->fps:15); }
    buf_t b={0};
    size_t moof=box_begin(&b,"moof");
    size_t mfhd=box_begin(&b,"mfhd"); b32(&b,0); b32(&b,++m->seq); box_end(&b,mfhd);
    size_t traf=box_begin(&b,"traf");
    size_t tfhd=box_begin(&b,"tfhd"); b32(&b,0x020000); b32(&b,1); box_end(&b,tfhd);
    size_t tfdt=box_begin(&b,"tfdt"); b32(&b,0x01000000); b32(&b,(uint32_t)(m->base_dt>>32)); b32(&b,(uint32_t)m->base_dt); box_end(&b,tfdt);
    size_t trun=box_begin(&b,"trun"); b32(&b,0x000701); b32(&b,(uint32_t)m->fn);
    size_t doff_pos=b.len; b32(&b,0);
    for(int i=0;i<m->fn;i++){ b32(&b,m->fs[i].dur); b32(&b,m->fs[i].size); b32(&b,m->fs[i].key?0x02000000u:0x00010000u); }
    box_end(&b,trun); box_end(&b,traf); box_end(&b,moof);
    uint32_t doff=(uint32_t)b.len+8;
    b.p[doff_pos]=doff>>24;b.p[doff_pos+1]=doff>>16;b.p[doff_pos+2]=doff>>8;b.p[doff_pos+3]=doff;
    fwrite(b.p,1,b.len,m->f); free(b.p);
    uint32_t msz=(uint32_t)(m->frag.len+8);
    uint8_t mh[8]={msz>>24,msz>>16,msz>>8,msz,'m','d','a','t'};
    fwrite(mh,1,8,m->f); fwrite(m->frag.p,1,m->frag.len,m->f);
    for(int i=0;i<m->fn;i++) m->base_dt+=m->fs[i].dur;
    m->frag.len=0; m->fn=0;
}
typedef struct { buf_t*out; int codec; uint32_t size; int key; uint8_t*vps,*sps,*pps; int*vl,*sl,*pl; } aup_t;
static void fmp4_nal(void*u,const uint8_t*nal,uint32_t nl){
    aup_t*a=u; int cc=a->codec; int t=cc==RSDK_CODEC_H265?(nal[0]>>1)&0x3f:nal[0]&0x1f;
    if(is_ps(cc,t)){
        if(cc==RSDK_CODEC_H265){ if(t==32&&nl<512){memcpy(a->vps,nal,nl);*a->vl=nl;}
            if(t==33&&nl<512){memcpy(a->sps,nal,nl);*a->sl=nl;} if(t==34&&nl<512){memcpy(a->pps,nal,nl);*a->pl=nl;} }
        else { if(t==7&&nl<512){memcpy(a->sps,nal,nl);*a->sl=nl;} if(t==8&&nl<512){memcpy(a->pps,nal,nl);*a->pl=nl;} }
        return; }
    if(!is_vcl(cc,t)) return;
    uint8_t L[4]={nl>>24,nl>>16,nl>>8,nl}; bmem(a->out,L,4); bmem(a->out,nal,nl);
    a->size+=4+nl; if(is_key(cc,t)) a->key=1;
}
static void*fmp4_open(const char*path,const rsdk_export_opt_t*opt){
    fmp4_t*m=calloc(1,sizeof*m); if(!m)return NULL;
    m->f=fopen(path,"wb"); if(!m->f){free(m);return NULL;}
    m->w=opt&&opt->width?opt->width:3840; m->h=opt&&opt->height?opt->height:2160;
    m->ts=opt&&opt->timescale?opt->timescale:90000; m->fps=opt&&opt->fps?opt->fps:15; m->codec=-1;
    fmp4_ftyp(m->f); return m;
}
static int fmp4_add(void*ctx,const uint8_t*annexb,uint32_t len,uint64_t pts,int key,int codec){
    fmp4_t*m=ctx; if(m->codec<0)m->codec=codec;
    buf_t au={0};
    aup_t a={.out=&au,.codec=m->codec,.vps=m->vps,.sps=m->sps,.pps=m->pps,.vl=&m->vlen,.sl=&m->slen,.pl=&m->plen};
    for_each_nal(annexb,len,fmp4_nal,&a);
    if(a.size==0){ free(au.p); return 0; }
    int kf=a.key||key;
    if(!m->init && m->slen>0) fmp4_write_init(m);
    if(kf && m->fn>0) fmp4_flush(m);
    bmem(&m->frag,au.p,au.len); free(au.p);
    if(m->fn==m->fcap){ m->fcap=m->fcap?m->fcap*2:64; m->fs=realloc(m->fs,m->fcap*sizeof(fsamp_t)); }
    m->fs[m->fn].size=a.size; m->fs[m->fn].pts=pts; m->fs[m->fn].key=kf; m->fs[m->fn].dur=0; m->fn++;
    return 0;
}
static int fmp4_finish(void*ctx){
    fmp4_t*m=ctx; if(!m->init && m->slen>0) fmp4_write_init(m);
    fmp4_flush(m); fclose(m->f); free(m->fs); free(m->frag.p); free(m); return 0;
}
static const rsdk_muxer_t FMP4_MUXER = { "fmp4", RSDK_EXPORT_FMP4, fmp4_open, fmp4_add, fmp4_finish };

/* ================= 格式注册表 ================= */
#define MAXMUX 8
static const rsdk_muxer_t *g_mux[MAXMUX]; static int g_nmux=0;
static void ensure_builtin(void){ if(g_nmux==0){ g_mux[g_nmux++]=&MP4_MUXER; g_mux[g_nmux++]=&FMP4_MUXER; } }
rsdk_err_t rsdk_backup_register(const rsdk_muxer_t *mux){
    ensure_builtin(); if(!mux||g_nmux>=MAXMUX) return RSDK_E_PARAM;
    g_mux[g_nmux++]=mux; return RSDK_OK;
}
static const rsdk_muxer_t* find_mux(int fmt){
    ensure_builtin();
    for(int i=0;i<g_nmux;i++) if(g_mux[i]->fmt==fmt) return g_mux[i];
    return NULL;
}

/* ================= 导出驱动 ================= */
static rsdk_err_t drive(const rsdk_muxer_t*mx,const rsdk_export_opt_t*opt,const char*path,
                        rsdk_player_t*p){
    void*ctx=mx->open(path,opt); if(!ctx) return RSDK_E_IO;
    rsdk_frame_hdr_t h; const uint8_t*data; uint32_t len; int frames=0;
    while(rsdk_play_next_frame(p,&h,&data,&len)==RSDK_OK){
        mx->add(ctx,data,len,h.pts,h.frame_type==RSDK_FRAME_I,h.codec); frames++;
    }
    int rc=mx->finish(ctx);
    return (rc==0&&frames>0)?RSDK_OK:(frames==0?RSDK_E_NOTFOUND:RSDK_E_IO);
}

rsdk_err_t rsdk_backup_export_seg(rsdk_dev_t*d,const rsdk_index_slot_t*seg,
                                  const rsdk_export_opt_t*opt,const char*path){
    if(!d||!seg||!path) return RSDK_E_PARAM;
    int fmt=opt?opt->fmt:RSDK_EXPORT_MP4;
    const rsdk_muxer_t*mx=find_mux(fmt); if(!mx) return RSDK_E_PARAM;
    rsdk_player_t*p; rsdk_err_t rc=rsdk_play_open(d,seg,&p); if(rc) return rc;
    rc=drive(mx,opt,path,p); rsdk_play_close(p); return rc;
}

rsdk_err_t rsdk_backup_export(rsdk_group_t*g,uint32_t t0,uint32_t t1,int chn,
                              const rsdk_export_opt_t*opt,const char*path){
    if(!g||!path) return RSDK_E_PARAM;
    int fmt=opt?opt->fmt:RSDK_EXPORT_MP4;
    const rsdk_muxer_t*mx=find_mux(fmt); if(!mx) return RSDK_E_PARAM;
    rsdk_index_slot_t segs[256];
    int ns=rsdk_group_query(g,t0,t1,chn,-1,segs,256);
    if(ns<=0) return RSDK_E_NOTFOUND;
    rsdk_group_player_t*gp; rsdk_err_t rc=rsdk_group_play_open(g,segs,ns,&gp); if(rc) return rc;
    void*ctx=mx->open(path,opt); if(!ctx){ rsdk_group_play_close(gp); return RSDK_E_IO; }
    rsdk_frame_hdr_t h; const uint8_t*data; uint32_t len; int disk,frames=0;
    while(rsdk_group_play_next(gp,&h,&data,&len,&disk)==RSDK_OK){
        mx->add(ctx,data,len,h.pts,h.frame_type==RSDK_FRAME_I,h.codec); frames++;
    }
    int frc=mx->finish(ctx);
    rsdk_group_play_close(gp);
    return (frc==0&&frames>0)?RSDK_OK:RSDK_E_IO;
}
