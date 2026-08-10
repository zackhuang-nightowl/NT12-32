/* Copyright (C) 2025-2026, Nightowl DG. RSDK AES-256-CTR(设计 §5).
 * 软件 AES-256(FIPS-197) + CTR; 硬件 /dev/crypto(mc-aes) 路径预留, 失败回退软件。 */
#include "rsdk_crypto.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- AES-256 (FIPS-197) ---------------- */
static const uint8_t SBOX[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static const uint8_t RCON[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static uint32_t subword(uint32_t w) {
    return (SBOX[(w>>24)&0xff]<<24)|(SBOX[(w>>16)&0xff]<<16)|(SBOX[(w>>8)&0xff]<<8)|SBOX[w&0xff];
}

void rsdk_aes256_key(const uint8_t key[32], uint32_t rk[60]) {
    int Nk = 8, Nr = 14, i;
    for (i = 0; i < Nk; i++)
        rk[i] = (key[4*i]<<24)|(key[4*i+1]<<16)|(key[4*i+2]<<8)|key[4*i+3];
    for (i = Nk; i < 4*(Nr+1); i++) {
        uint32_t t = rk[i-1];
        if (i % Nk == 0)      t = subword((t<<8)|(t>>24)) ^ ((uint32_t)RCON[i/Nk] << 24);
        else if (i % Nk == 4) t = subword(t);
        rk[i] = rk[i-Nk] ^ t;
    }
}

static uint8_t xtime(uint8_t x) { return (uint8_t)((x<<1) ^ ((x>>7) * 0x1b)); }

void rsdk_aes256_encrypt_block(const uint32_t rk[60], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16]; int r, c;
    memcpy(s, in, 16);
    #define ARK(round) do { for (c=0;c<4;c++){ uint32_t w=rk[(round)*4+c]; \
        s[4*c]^=w>>24; s[4*c+1]^=w>>16; s[4*c+2]^=w>>8; s[4*c+3]^=w; } } while(0)
    ARK(0);
    for (r = 1; r <= 14; r++) {
        for (c = 0; c < 16; c++) s[c] = SBOX[s[c]];             /* SubBytes */
        uint8_t t[16];                                          /* ShiftRows */
        for (c = 0; c < 4; c++) {
            t[4*c+0]=s[4*c+0]; t[4*c+1]=s[4*((c+1)%4)+1];
            t[4*c+2]=s[4*((c+2)%4)+2]; t[4*c+3]=s[4*((c+3)%4)+3];
        }
        memcpy(s, t, 16);
        if (r != 14) for (c = 0; c < 4; c++) {                  /* MixColumns */
            uint8_t *col = s + 4*c;
            uint8_t a0=col[0],a1=col[1],a2=col[2],a3=col[3];
            col[0]=xtime(a0)^(xtime(a1)^a1)^a2^a3;
            col[1]=a0^xtime(a1)^(xtime(a2)^a2)^a3;
            col[2]=a0^a1^xtime(a2)^(xtime(a3)^a3);
            col[3]=(xtime(a0)^a0)^a1^a2^xtime(a3);
        }
        ARK(r);
    }
    #undef ARK
    memcpy(out, s, 16);
}

/* ---------------- 硬件 /dev/crypto (mc-aes) 可选 ---------------- */
#if defined(__has_include)
#  if __has_include(<crypto/cryptodev.h>)
#    define RSDK_HAVE_CRYPTODEV 1
#    include <crypto/cryptodev.h>
#    include <sys/ioctl.h>
#    include <fcntl.h>
#    include <unistd.h>
#  endif
#endif

/* ---------------- CTR + 句柄 ---------------- */
struct rsdk_crypto {
    uint32_t rk[60];       /* 软件 AES 轮密钥(回退/自检) */
    int      cfd;          /* /dev/crypto fd; -1=软件 */
    uint32_t ses;          /* 硬件会话 */
};

rsdk_err_t rsdk_crypto_open(const uint8_t dek[32], int prefer_hw, rsdk_crypto_t **out) {
    if (!dek || !out) return RSDK_E_PARAM;
    rsdk_crypto_t *c = calloc(1, sizeof(*c));
    if (!c) return RSDK_E_CRYPTO;
    rsdk_aes256_key(dek, c->rk);
    c->cfd = -1;
#ifdef RSDK_HAVE_CRYPTODEV
    if (prefer_hw) {                                   /* 尝试硬件 AES-256-CTR */
        int fd = open("/dev/crypto", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            struct session_op s; memset(&s, 0, sizeof s);
            s.cipher = CRYPTO_AES_CTR; s.keylen = 32; s.key = (void*)dek;
            if (ioctl(fd, CIOCGSESSION, &s) == 0) { c->cfd = fd; c->ses = s.ses; }
            else close(fd);
        }
    }
#else
    (void)prefer_hw;
#endif
    *out = c;
    return RSDK_OK;
}
void rsdk_crypto_close(rsdk_crypto_t *c) {
    if (!c) return;
#ifdef RSDK_HAVE_CRYPTODEV
    if (c->cfd >= 0) { ioctl(c->cfd, CIOCFSESSION, &c->ses); close(c->cfd); }
#endif
    memset(c->rk, 0, sizeof c->rk); free(c);
}

/* counter = | seg_id(32) | frame_seq(32) | block64 |, block64 从 off/16 起 */
rsdk_err_t rsdk_crypto_xcrypt(rsdk_crypto_t *c, uint32_t seg_id, uint32_t frame_seq,
                              uint64_t off, uint8_t *buf, size_t len) {
    if (!c || (!buf && len)) return RSDK_E_PARAM;
    uint64_t blk = off / 16; size_t skip = off % 16;
#ifdef RSDK_HAVE_CRYPTODEV
    if (c->cfd >= 0 && skip == 0 && len) {             /* 硬件 AES-256-CTR(块对齐) */
        uint8_t iv[16];
        iv[0]=seg_id>>24; iv[1]=seg_id>>16; iv[2]=seg_id>>8; iv[3]=seg_id;
        iv[4]=frame_seq>>24; iv[5]=frame_seq>>16; iv[6]=frame_seq>>8; iv[7]=frame_seq;
        for (int i=0;i<8;i++) iv[8+i]=(uint8_t)(blk>>(56-8*i));
        struct crypt_op op; memset(&op,0,sizeof op);
        op.ses=c->ses; op.op=COP_ENCRYPT; op.len=len; op.src=buf; op.dst=buf; op.iv=iv;
        if (ioctl(c->cfd, CIOCCRYPT, &op) == 0) return RSDK_OK;
        /* 硬件失败 → 落到软件(下方) */
    }
#endif
    uint8_t ctr[16], ks[16]; size_t done = 0;
    while (done < len) {
        ctr[0]=seg_id>>24; ctr[1]=seg_id>>16; ctr[2]=seg_id>>8; ctr[3]=seg_id;
        ctr[4]=frame_seq>>24; ctr[5]=frame_seq>>16; ctr[6]=frame_seq>>8; ctr[7]=frame_seq;
        for (int i = 0; i < 8; i++) ctr[8+i] = (uint8_t)(blk >> (56 - 8*i));
        rsdk_aes256_encrypt_block(c->rk, ctr, ks);
        for (size_t i = skip; i < 16 && done < len; i++, done++)
            buf[done] ^= ks[i];
        skip = 0; blk++;
    }
    return RSDK_OK;
}

/* ---------------- SHA-256 / HMAC-SHA256 / PBKDF2 (零依赖, 纯软件) ---------------- */
#define RSDK_KDF_ITERS 10000

/* SHA-256 内部宏 */
#define RR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(e,f,g)  (((e)&(f))^(~(e)&(g)))
#define MAJ(a,b,c) (((a)&(b))^((a)&(c))^((b)&(c)))
#define EP0(a) (RR32(a,2)^RR32(a,13)^RR32(a,22))
#define EP1(e) (RR32(e,6)^RR32(e,11)^RR32(e,25))
#define SIG0(x)(RR32(x,7)^RR32(x,18)^((x)>>3))
#define SIG1(x)(RR32(x,17)^RR32(x,19)^((x)>>10))

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct { uint8_t buf[64]; uint32_t h[8]; uint64_t len; uint32_t fill; } rsdk_sha256_ctx;

static void sha256_init(rsdk_sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->len = 0; c->fill = 0;
}

static void sha256_compress(rsdk_sha256_ctx *c) {
    uint32_t w[64], s[8], t1, t2;
    for (int i=0;i<16;i++)
        w[i]=((uint32_t)c->buf[4*i]<<24)|((uint32_t)c->buf[4*i+1]<<16)
            |((uint32_t)c->buf[4*i+2]<<8)|(uint32_t)c->buf[4*i+3];
    for (int i=16;i<64;i++) w[i]=SIG1(w[i-2])+w[i-7]+SIG0(w[i-15])+w[i-16];
    for (int i=0;i<8;i++) s[i]=c->h[i];
    for (int i=0;i<64;i++) {
        t1=s[7]+EP1(s[4])+CH(s[4],s[5],s[6])+SHA256_K[i]+w[i];
        t2=EP0(s[0])+MAJ(s[0],s[1],s[2]);
        s[7]=s[6]; s[6]=s[5]; s[5]=s[4]; s[4]=s[3]+t1;
        s[3]=s[2]; s[2]=s[1]; s[1]=s[0]; s[0]=t1+t2;
    }
    for (int i=0;i<8;i++) c->h[i]+=s[i];
}

static void sha256_update(rsdk_sha256_ctx *c, const uint8_t *d, size_t n) {
    for (size_t i=0;i<n;i++) {
        c->buf[c->fill++]=(uint8_t)d[i];
        if (c->fill==64) { sha256_compress(c); c->fill=0; }
    }
    c->len+=n;
}

static void sha256_final(rsdk_sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits=c->len*8;
    uint8_t p=0x80; sha256_update(c,&p,1);
    while(c->fill!=56){ p=0; sha256_update(c,&p,1); }
    for (int i=7;i>=0;i--){ p=(uint8_t)(bits>>(8*i)); sha256_update(c,&p,1); }
    for (int i=0;i<8;i++){
        out[4*i]=(uint8_t)(c->h[i]>>24); out[4*i+1]=(uint8_t)(c->h[i]>>16);
        out[4*i+2]=(uint8_t)(c->h[i]>>8); out[4*i+3]=(uint8_t)c->h[i];
    }
}

/* 公开接口: SHA-256(data,len)→out[32] */
void rsdk_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    rsdk_sha256_ctx c; sha256_init(&c); sha256_update(&c,data,len); sha256_final(&c,out);
}

/* HMAC-SHA256(key,klen, msg,mlen)→mac[32] */
void rsdk_hmac_sha256(const uint8_t *key, size_t klen,
                      const uint8_t *msg, size_t mlen, uint8_t mac[32]) {
    uint8_t k0[64], ipad[64], opad[64];
    memset(k0,0,64);
    if (klen>64) rsdk_sha256(key,klen,k0); else memcpy(k0,key,klen);
    for (int i=0;i<64;i++){ ipad[i]=k0[i]^0x36; opad[i]=k0[i]^0x5c; }
    rsdk_sha256_ctx c; sha256_init(&c);
    sha256_update(&c,ipad,64); sha256_update(&c,msg,mlen); sha256_final(&c,mac);
    sha256_init(&c);
    sha256_update(&c,opad,64); sha256_update(&c,mac,32); sha256_final(&c,mac);
}

/* HMAC-SHA256 流式版: 支持分块消息(先 salt 后 INT(i)) */
static void hmac_sha256_2(const uint8_t *key, size_t klen,
                          const uint8_t *m1, size_t m1len,
                          const uint8_t *m2, size_t m2len,
                          uint8_t mac[32]) {
    uint8_t k0[64], ipad[64], opad[64];
    memset(k0,0,64);
    if (klen>64) rsdk_sha256(key,klen,k0); else memcpy(k0,key,klen);
    for (int i=0;i<64;i++){ ipad[i]=k0[i]^0x36; opad[i]=k0[i]^0x5c; }
    rsdk_sha256_ctx c; sha256_init(&c);
    sha256_update(&c,ipad,64);
    sha256_update(&c,m1,m1len);
    sha256_update(&c,m2,m2len);
    sha256_final(&c,mac);
    sha256_init(&c);
    sha256_update(&c,opad,64); sha256_update(&c,mac,32); sha256_final(&c,mac);
}

/* PBKDF2-HMAC-SHA256: RFC 2898 §5.2; 任意 salt 长度, 任意 dklen */
void rsdk_pbkdf2_sha256(const uint8_t *pass, size_t plen,
                        const uint8_t *salt, size_t slen,
                        uint32_t iters, uint8_t *dk, size_t dklen) {
    size_t hlen = 32;
    uint32_t nblocks = (uint32_t)((dklen + hlen - 1) / hlen);
    for (uint32_t blk = 1; blk <= nblocks; blk++) {
        uint8_t ibuf[4];
        ibuf[0]=(uint8_t)(blk>>24); ibuf[1]=(uint8_t)(blk>>16);
        ibuf[2]=(uint8_t)(blk>>8);  ibuf[3]=(uint8_t)blk;
        uint8_t u[32], t[32];
        /* U_1 = PRF(P, S || INT(i)) — 流式两段 HMAC */
        hmac_sha256_2(pass, plen, salt, slen, ibuf, 4, u);
        memcpy(t, u, 32);
        for (uint32_t j=1; j<iters; j++) {
            rsdk_hmac_sha256(pass, plen, u, 32, u);
            for (int x=0;x<32;x++) t[x]^=u[x];
        }
        size_t off=(blk-1)*hlen, take=dklen-off; if(take>hlen)take=hlen;
        memcpy(dk+off, t, take);
    }
}

/* ---------------- 简化 KEK/DEK 封装(设计 §5.2; 非 AES-KW, 工程可替换) ---------------- */
void rsdk_kdf_kek(const char *sn, const uint8_t salt[16], uint8_t kek[32]) {
    /* CBC-MAC(AES) over (salt || sn) 产两块 = 32B; 用固定弱密钥引导, 仅演示确定性派生 */
    uint8_t seed[32]; memset(seed, 0x5a, 32);
    for (int i = 0; i < 16; i++) seed[i] ^= salt[i];
    if (sn) for (int i = 0; sn[i] && i < 16; i++) seed[16+i] ^= (uint8_t)sn[i];
    uint32_t rk[60]; rsdk_aes256_key(seed, rk);
    uint8_t iv[16]; memset(iv, 0, 16);
    rsdk_aes256_encrypt_block(rk, iv, kek);          /* 前 16B */
    for (int i = 0; i < 16; i++) iv[i] = kek[i] ^ 0xff;
    rsdk_aes256_encrypt_block(rk, iv, kek+16);       /* 后 16B */
}

/* KDF 调度器: kdf_id=0→legacy, kdf_id=1→PBKDF2-HMAC-SHA256 */
void rsdk_kdf_kek2(const char *sn, const uint8_t salt[16], uint32_t kdf_id, uint8_t kek[32]) {
    if (kdf_id == 0) {
        rsdk_kdf_kek(sn, salt, kek);
    } else {
        /* kdf_id==1: PBKDF2-HMAC-SHA256(password=sn, salt=salt[16], iters=RSDK_KDF_ITERS, dkLen=32) */
        size_t plen = sn ? strlen(sn) : 0;
        rsdk_pbkdf2_sha256((const uint8_t *)(sn ? sn : ""), plen,
                           salt, 16, RSDK_KDF_ITERS, kek, 32);
    }
}

void rsdk_dek_wrap(const uint8_t kek[32], const uint8_t dek[32], uint8_t wrapped[48], uint8_t kcv[8]) {
    rsdk_crypto_t *c; rsdk_crypto_open(kek, 0, &c);
    memcpy(wrapped, dek, 32);
    rsdk_crypto_xcrypt(c, 0xA5A5A5A5u, 0, 0, wrapped, 32);   /* CTR 封装 DEK */
    memset(wrapped+32, 0, 16);
    uint8_t zero[16] = {0}, kc[16]; rsdk_aes256_encrypt_block(((struct rsdk_crypto*)c)->rk, zero, kc);
    memcpy(kcv, kc, 8);
    rsdk_crypto_close(c);
}

rsdk_err_t rsdk_dek_unwrap(const uint8_t kek[32], const uint8_t wrapped[48], const uint8_t kcv[8], uint8_t dek[32]) {
    rsdk_crypto_t *c; rsdk_crypto_open(kek, 0, &c);
    uint8_t zero[16] = {0}, kc[16]; rsdk_aes256_encrypt_block(((struct rsdk_crypto*)c)->rk, zero, kc);
    if (memcmp(kc, kcv, 8) != 0) { rsdk_crypto_close(c); return RSDK_E_CRYPTO; }
    memcpy(dek, wrapped, 32);
    rsdk_crypto_xcrypt(c, 0xA5A5A5A5u, 0, 0, dek, 32);
    rsdk_crypto_close(c);
    return RSDK_OK;
}
