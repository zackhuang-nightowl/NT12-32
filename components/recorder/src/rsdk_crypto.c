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
