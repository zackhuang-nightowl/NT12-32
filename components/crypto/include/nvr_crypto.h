/***************************************************************************************
 *  nvr_crypto.h — 密码/加密算法薄封装（MD5 / SHA / AES-256）
 *
 *  用途（计划 §B1.2 / BIND_IPC_FLOW §5）：
 *    即插即用鉴权的三处密码派生：
 *      ① 增强模式 random → password（NOP 私有算法）
 *      ② 激活 X_NightOwl_setDeviceActive 的 AES256 口令加密
 *      ③ 8012 / ONVIF digest（MD5 / SHA）
 *
 *  底层用 OpenSSL libcrypto（EVP）。原语（md5/sha/aes）为通用实现，已用测试向量校验。
 *
 *  ⚠️ nvr_pw_from_random() / nvr_pw_aes256_activate() 的**确切算法与密钥属 NightOwl 私有**
 *     （导图注「请联系 NightOwl-DG 办公室」）。当前为**占位实现**，接口稳定，待 DG 提供
 *     算法与向量后在 nvr_crypto.c 内替换实现即可，调用方无需改动。
 ***************************************************************************************/
#ifndef NVR_CRYPTO_H
#define NVR_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 摘要原语 ---------------- */
#define NVR_MD5_LEN     16
#define NVR_SHA1_LEN    20
#define NVR_SHA256_LEN  32

int nvr_md5   (const void *data, size_t len, uint8_t out[NVR_MD5_LEN]);
int nvr_sha1  (const void *data, size_t len, uint8_t out[NVR_SHA1_LEN]);
int nvr_sha256(const void *data, size_t len, uint8_t out[NVR_SHA256_LEN]);

/* 十六进制（小写）；out 需 2*len+1 字节。返回 out。 */
char *nvr_hex(const uint8_t *in, size_t len, char *out, size_t out_cap);

/* ---------------- AES-256 ---------------- */
/* key 32 字节。CBC 需 iv 16 字节；ECB iv 传 NULL。len 必须是 16 的倍数（调用方自行 PKCS 或补齐）。
 * out 至少 len 字节。返回输出字节数，<0 失败。 */
int nvr_aes256_cbc_enc(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t *in, int len, uint8_t *out);
int nvr_aes256_cbc_dec(const uint8_t key[32], const uint8_t iv[16],
                       const uint8_t *in, int len, uint8_t *out);
int nvr_aes256_ecb_enc(const uint8_t key[32], const uint8_t *in, int len, uint8_t *out);
int nvr_aes256_ecb_dec(const uint8_t key[32], const uint8_t *in, int len, uint8_t *out);

/* ---------------- BLE AES-128-ECB（对齐 BLE_helper Rijndael：key/IV=password UTF-8 前 16B 零填充）---- */
/* 明文 zero-pad 到 16 对齐后加密。返回密文长度，<0 失败。out_cap 须 ≥ ((in_len+15)&~15)。 */
int nvr_ble_aes_enc(const char *password, const uint8_t *in, int in_len,
                    uint8_t *out, int out_cap);
/* 密文长度会补齐到 16 对齐再解；输出可能含尾零。返回明文长度，<0 失败。 */
int nvr_ble_aes_dec(const char *password, const uint8_t *in, int in_len,
                    uint8_t *out, int out_cap);

/* ---------------- 口令派生（NightOwl；密钥/盐在 nvr_crypto.c 顶部填写） ----------------
 * 见 docs/BIND_IPC_FLOW.md §5。private 常量：NVR_ENH_KEY_X / NVR_ENH_KEY_Y（增强），
 * NVR_ACT_AES_KEY（激活 AES-256-ECB，只要 key）——在 nvr_crypto.c 顶部。 */

/* ① 增强安全模式鉴权密码 P_enh：first16Alnum(SHA256(KEY_X + random + KEY_Y))。
 *   用于 ONVIF/8089/RTSP/7000 的 Digest 密码（账户固定 admin）。返回长度(=16)，<0 失败。 */
int nvr_pw_from_random(const char *random, char *out, size_t out_cap);
/* 核心可测版：显式传 key_x/key_y（自测用文档向量校验）。 */
int nvr_pw_enhanced_calc(const char *key_x, const char *random, const char *key_y,
                         char *out, size_t out_cap);

/* ② 8012 消息中心 增强 digest：first16Alnum(MD5(username_ts : "8012" : auth_pwd))。
 *   auth_pwd 即 P_enh。username_ts 为 client 生成的 unix 时间戳字符串。 */
int nvr_pw_8012_digest(const char *username_ts, const char *auth_pwd, char *out, size_t out_cap);

/* ③ nopOnvif 激活密码（明文 P_act）：UPPER(first16(MD5(nvr_sn + dev_sn)))。
 *   激活成功后即成为设备密码，NVR 之后用 admin/P_act 连接。返回长度(=16)，<0 失败。 */
int nvr_pw_activate(const char *nvr_sn, const char *dev_sn, char *out, size_t out_cap);

/* ④ setDeviceActive 密码字段：AES-256-ECB(P_act) 的十六进制串（IPC 解密后设为自身密码）。
 *   只要 NVR_ACT_AES_KEY（ASCII 补 0 到 32 字节）。返回 hex 长度，<0 失败。 */
int nvr_pw_act_encrypt(const char *nvr_sn, const char *dev_sn, char *out, size_t out_cap);

/* 取「前 N 个字母数字字符」（对 hex 串即前 N 个字符）。工具，供上层复用。 */
int nvr_first_alnum(const char *in, int n, char *out, size_t out_cap);

/* 运行期覆盖密钥（设置库 factory.keyx/keyy / factory.act_aes_key/iv）。
 * aes_*_hex 为 32/16 字节的 hex；可空则只改增强前后缀。 */
int nvr_pw_set_keys(const char *key_x, const char *key_y,
                    const char *aes_key_hex, const char *aes_iv_hex);

int nvr_pw_enh_ready(void);   /* 增强 KEY_X/Y 已就绪 */
int nvr_pw_act_ready(void);   /* 激活 AES key 已就绪 */
int nvr_pw_algo_ready(void);  /* 增强就绪（激活 AES 另见 nvr_pw_act_ready） */

#ifdef __cplusplus
}
#endif
#endif /* NVR_CRYPTO_H */
