/* Copyright (C) 2025-2026, Nightowl DG. RSDK AES-256-CTR(设计 §5). */
#ifndef RSDK_CRYPTO_H
#define RSDK_CRYPTO_H
#include "rsdk_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsdk_crypto rsdk_crypto_t;

/* dek: 32B 数据密钥; prefer_hw!=0 时优先 /dev/crypto(mc-aes), 失败软件回退 */
RSDK_API rsdk_err_t rsdk_crypto_open (const uint8_t dek[32], int prefer_hw, rsdk_crypto_t **out);
void       rsdk_crypto_close(rsdk_crypto_t *c);

/* 就地 CTR 加/解密(对称): counter = f(seg_id, frame_seq) + off/16。
 * off 为该帧负载内字节偏移(回放 seek 时可非 0)。加解密同一函数。 */
RSDK_API rsdk_err_t rsdk_crypto_xcrypt(rsdk_crypto_t *c, uint32_t seg_id, uint32_t frame_seq,
                                       uint64_t off, uint8_t *buf, size_t len);

/* 工具: 由设备SN+salt 派生 KEK, 并解/封装 DEK(演示用简化 KDF) */
RSDK_API void rsdk_kdf_kek(const char *sn, const uint8_t salt[16], uint8_t kek[32]);
RSDK_API void rsdk_dek_wrap  (const uint8_t kek[32], const uint8_t dek[32], uint8_t wrapped[48], uint8_t kcv[8]);
RSDK_API rsdk_err_t rsdk_dek_unwrap(const uint8_t kek[32], const uint8_t wrapped[48], const uint8_t kcv[8], uint8_t dek[32]);

/* 低层 AES-256 单块(供自测/封装) */
RSDK_API void rsdk_aes256_key(const uint8_t key[32], uint32_t rk[60]);
RSDK_API void rsdk_aes256_encrypt_block(const uint32_t rk[60], const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif
#endif
