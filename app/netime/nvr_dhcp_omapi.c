/* nvr_dhcp_omapi.c —— 自包含 OMAPI 客户端:PoE 口相机离线时,主动释放该口对应
 * 网段的 DHCP 租约,使换机/重连的相机能立即拿到 IP(不必等 300s 租约到期)。
 *
 * 背景(见 docs/PoE_VLAN_PVID映射与DHCP_2026-09-02.md):每段单地址池 range x.1 x.1,
 * ISC dhcpd 只在租约到期才释放、不主动检测离线 → 旧机离线后仍占着唯一地址,新机 no free。
 * 设备无 omshell,但 dhcpd(4.4.3-P1)编译带 OMAPI。本文件用已链接的 openssl(HMAC-MD5)
 * 手写最小 OMAPI 客户端,连本机 dhcpd 的 omapi-port 释放指定地址的租约。
 *
 * 触发:nvr_channel.c clear_poe_port()(poe_miss≥阈值,确认相机移除)→ nvr_dhcp_release_seg(port)。
 *
 * dhcpd 侧需在 /etc/dhcpd_vlan.conf 配:
 *   omapi-port 7911;
 *   key nvr_omapi { algorithm hmac-md5; secret "bnZyT21hcGlLZXkxMjM0NQ=="; }
 *   omapi-key nvr_omapi;
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/hmac.h>
#include "nvr_log.h"
#include "nvr_defaults.h"

#define OMAPI_HOST      "127.0.0.1"
#define OMAPI_PORT      7911
#define OMAPI_OP_OPEN   1
#define OMAPI_OP_UPDATE 3
#define OMAPI_OP_STATUS 5

/* 与 dhcpd_vlan.conf 的 key 一致:name=nvr_omapi,secret base64=bnZyT21hcGlLZXkxMjM0NQ==
 * = 原始 16 字节 ASCII "nvrOmapiKey12345"。 */
static const char          OMAPI_KEYNAME[] = "nvr_omapi";
static const unsigned char OMAPI_SECRET[]  = "nvrOmapiKey12345"; /* 16 bytes, no NUL */
#define OMAPI_SECRET_LEN 16
#define OMAPI_SIG_LEN    16   /* HMAC-MD5 输出 */

/* ---- 可增长缓冲 ---- */
typedef struct { unsigned char *b; size_t len, cap; } buf_t;
static int bgrow(buf_t *B, size_t n){
    if (B->len + n > B->cap){ size_t nc=(B->len+n)*2+64; unsigned char *p=realloc(B->b,nc); if(!p)return -1; B->b=p; B->cap=nc; }
    return 0;
}
static int bput(buf_t *B, const void *d, size_t n){ if(bgrow(B,n))return -1; memcpy(B->b+B->len,d,n); B->len+=n; return 0; }
static int bu16(buf_t *B, uint16_t v){ v=htons(v); return bput(B,&v,2); }
static int bu32(buf_t *B, uint32_t v){ v=htonl(v); return bput(B,&v,4); }
/* name/value 对:uint16 nlen, name, uint32 vlen, val */
static int bnv(buf_t *B, const char *name, const void *val, uint32_t vlen){
    size_t nl=strlen(name);
    if(bu16(B,(uint16_t)nl)||bput(B,name,nl)||bu32(B,vlen))return -1;
    return vlen? bput(B,val,vlen):0;
}

/* ---- 阻塞收发 ---- */
static int rd_n(int fd, void *buf, size_t n){
    unsigned char *p=buf; size_t got=0;
    while(got<n){ ssize_t r=read(fd,p+got,n-got); if(r<=0) return -1; got+=(size_t)r; }
    return 0;
}
static int wr_all(int fd, const void *buf, size_t n){
    const unsigned char *p=buf; size_t sent=0;
    while(sent<n){ ssize_t w=write(fd,p+sent,n-sent); if(w<=0) return -1; sent+=(size_t)w; }
    return 0;
}

/* 发一条 OMAPI 消息:body=op,handle,tid,rid,msgvalues,0x0000,objvalues,0x0000
 * 帧 = authid(4) authlen(4) body [sig]。sign!=0 时对 body 做 HMAC-MD5 附在末尾。 */
static int send_msg(int fd, uint32_t authid, int sign, const buf_t *body){
    buf_t frame={0};
    unsigned char sig[OMAPI_SIG_LEN];
    if(bu32(&frame,authid)) goto err;
    if(bu32(&frame, sign?OMAPI_SIG_LEN:0)) goto err;
    if(bput(&frame,body->b,body->len)) goto err;
    if(sign){
        unsigned int ml=OMAPI_SIG_LEN;
        /* ★ ISC OMAPI 签名覆盖 authlen 字段 + body(不含 authid)。此时 frame =
         * authid(4)+authlen(4)+body,故签名数据 = frame.b+4, 长度 frame.len-4。 */
        if(!HMAC(EVP_md5(),OMAPI_SECRET,OMAPI_SECRET_LEN, frame.b+4, frame.len-4, sig,&ml)) goto err;
        if(bput(&frame,sig,OMAPI_SIG_LEN)) goto err;
    }
    if(wr_all(fd,frame.b,frame.len)) goto err;
    free(frame.b);
    return 0;
err:
    free(frame.b);
    return -1;
}

/* 读一条 OMAPI 消息头部(24字节)+ 跳过 message/object 值 + 签名。
 * 返回 op;out_handle 回填对象句柄;out_result 回填 STATUS 的 result(若有)。 */
static int recv_msg(int fd, uint32_t *out_handle, uint32_t *out_result){
    uint32_t hdr[6];
    if(rd_n(fd,hdr,sizeof(hdr))) return -1;
    uint32_t authlen = ntohl(hdr[1]);
    uint32_t op      = ntohl(hdr[2]);
    uint32_t handle  = ntohl(hdr[3]);
    if(out_handle) *out_handle=handle;
    if(out_result) *out_result=0xffffffff;
    /* 读两段 name/value(message,object),遇 nlen==0 结束 */
    for(int seg=0; seg<2; seg++){
        for(;;){
            uint16_t nl; if(rd_n(fd,&nl,2)) return -1; nl=ntohs(nl);
            if(nl==0) break;
            char name[64]; uint32_t vl;
            unsigned rd = nl<sizeof(name)-1? nl : sizeof(name)-1;
            if(rd_n(fd,name,rd)) return -1; name[rd]=0;
            if(nl>rd){ char skip[256]; uint32_t left=nl-rd; while(left){uint32_t c=left<sizeof(skip)?left:sizeof(skip); if(rd_n(fd,skip,c))return -1; left-=c;} }
            if(rd_n(fd,&vl,4)) return -1; vl=ntohl(vl);
            /* 抓 STATUS 的 result(uint32) */
            if(seg==0 && vl==4 && strcmp(name,"result")==0){
                uint32_t rv; if(rd_n(fd,&rv,4)) return -1; if(out_result)*out_result=ntohl(rv); continue;
            }
            char skip[256]; uint32_t left=vl; while(left){uint32_t c=left<sizeof(skip)?left:sizeof(skip); if(rd_n(fd,skip,c))return -1; left-=c;}
        }
    }
    if(authlen){ char skip[64]; uint32_t left=authlen; while(left){uint32_t c=left<sizeof(skip)?left:sizeof(skip); if(rd_n(fd,skip,c))return -1; left-=c;} }
    (void)handle;
    return (int)op;
}

/* 释放 198.18.<seg>.1 的租约(seg=PoE 口号)。成功返回 0。 */
int nvr_dhcp_release_seg(int seg){
    if(seg<1 || seg>16) return -1;   /* PoE 最多 16 口(NVR_POE_PORTS) */
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) return -1;
    struct timeval tv={2,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; sa.sin_port=htons(OMAPI_PORT);
    sa.sin_addr.s_addr=inet_addr(OMAPI_HOST);
    if(connect(fd,(struct sockaddr*)&sa,sizeof(sa))){ close(fd); return -1; }

    int rc=-1; uint32_t tid=1, authid=0, lease_handle=0, op, result;
    buf_t body={0};

    /* 1) startup:version=100, header_size=24 */
    uint32_t su[2]={htonl(100),htonl(24)};
    if(wr_all(fd,su,sizeof(su))) goto out;
    if(rd_n(fd,su,sizeof(su))) goto out;            /* 对方 startup */
    if(ntohl(su[0])!=100) goto out;

    /* 2) OPEN authenticator(不签名)→ 拿 authid(返回句柄) */
    body.len=0;
    if(bu32(&body,OMAPI_OP_OPEN)||bu32(&body,0)||bu32(&body,tid++)||bu32(&body,0)) goto out;
    { const char *t="authenticator"; if(bnv(&body,"type",t,strlen(t))) goto out; }
    if(bu16(&body,0)) goto out;                     /* message 段结束 */
    if(bnv(&body,"name",OMAPI_KEYNAME,strlen(OMAPI_KEYNAME))) goto out;
    /* ★ algorithm 必须用 TSIG 全名,dhcpd 内部即以此名注册 key(否则"no object matches") */
    { const char *a="hmac-md5.SIG-ALG.REG.INT."; if(bnv(&body,"algorithm",a,strlen(a))) goto out; }
    if(bu16(&body,0)) goto out;                     /* object 段结束 */
    if(send_msg(fd,0,0,&body)) goto out;
    op=recv_msg(fd,&authid,&result);
    if(op!=OMAPI_OP_UPDATE || authid==0){ NVR_LOGW("dhcp","OMAPI 认证失败 op=%u",op); goto out; }

    /* 3) OPEN lease by ip-address(签名)→ 拿 lease 句柄 */
    body.len=0;
    if(bu32(&body,OMAPI_OP_OPEN)||bu32(&body,0)||bu32(&body,tid++)||bu32(&body,0)) goto out;
    { const char *t="lease"; if(bnv(&body,"type",t,strlen(t))) goto out; }
    if(bu16(&body,0)) goto out;
    { uint32_t ip=inet_addr("0.0.0.0"); char pat[24];
      snprintf(pat,sizeof(pat),"%s.%d.1",NVR_POE_NET_PREFIX,seg);
      ip=inet_addr(pat);                            /* 网络序 4 字节 */
      if(bnv(&body,"ip-address",&ip,4)) goto out; }
    if(bu16(&body,0)) goto out;
    if(send_msg(fd,authid,1,&body)) goto out;
    op=recv_msg(fd,&lease_handle,&result);
    if(op!=OMAPI_OP_UPDATE || lease_handle==0){
        NVR_LOGI("dhcp","口%d 无活动租约可释放(op=%u)",seg,op); rc=0; goto out;  /* 没租约=已达目的 */
    }

    /* 4) UPDATE lease:置 state=free(1)→ 释放地址。对 active 和 abandoned 都有效
     * (ends=0 对 abandoned 会被 dhcpd 重新标 abandoned,state=1 才彻底 free)。 */
    body.len=0;
    if(bu32(&body,OMAPI_OP_UPDATE)||bu32(&body,lease_handle)||bu32(&body,tid++)||bu32(&body,0)) goto out;
    if(bu16(&body,0)) goto out;                     /* message 段空 */
    { uint32_t st=htonl(1); if(bnv(&body,"state",&st,4)) goto out; }
    if(bu16(&body,0)) goto out;
    if(send_msg(fd,authid,1,&body)) goto out;
    op=recv_msg(fd,NULL,&result);
    if(op==OMAPI_OP_STATUS && result!=0){ NVR_LOGW("dhcp","口%d 释放租约 STATUS=%u",seg,result); goto out; }

    NVR_LOGI("dhcp","口%d 释放 %s.%d.1 租约(OMAPI,离线换机可即时获取)",seg,NVR_POE_NET_PREFIX,seg);
    rc=0;
out:
    free(body.b);
    close(fd);
    return rc;
}
