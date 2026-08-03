/***************************************************************************************
 *  nvr_crypto.c — MD5/SHA/AES-256 封装（OpenSSL EVP）+ 口令派生占位。见 nvr_crypto.h。
 ***************************************************************************************/
#include "nvr_crypto.h"

#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

/* ---------------- 摘要 ---------------- */
static int digest(const EVP_MD *md, const void *data, size_t len, uint8_t *out)
{
    unsigned int n = 0;
    if (EVP_Digest(data, len, out, &n, md, NULL) != 1) return -1;
    return (int)n;
}
int nvr_md5   (const void *d, size_t l, uint8_t o[NVR_MD5_LEN])    { return digest(EVP_md5(),    d, l, o) == NVR_MD5_LEN    ? 0 : -1; }
int nvr_sha1  (const void *d, size_t l, uint8_t o[NVR_SHA1_LEN])   { return digest(EVP_sha1(),   d, l, o) == NVR_SHA1_LEN   ? 0 : -1; }
int nvr_sha256(const void *d, size_t l, uint8_t o[NVR_SHA256_LEN]) { return digest(EVP_sha256(), d, l, o) == NVR_SHA256_LEN ? 0 : -1; }

char *nvr_hex(const uint8_t *in, size_t len, char *out, size_t out_cap)
{
    static const char *H = "0123456789abcdef";
    size_t i = 0;
    for (; i < len && (2 * i + 2) < out_cap; i++) {
        out[2 * i]     = H[(in[i] >> 4) & 0xF];
        out[2 * i + 1] = H[in[i] & 0xF];
    }
    out[2 * i] = 0;
    return out;
}

/* ---------------- AES-256 ---------------- */
static int aes_run(const EVP_CIPHER *c, int enc, const uint8_t key[32], const uint8_t iv[16],
                   const uint8_t *in, int len, uint8_t *out)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    int rc = -1, outl = 0, tmpl = 0;
    if (EVP_CipherInit_ex(ctx, c, NULL, key, iv, enc) != 1) goto done;
    EVP_CIPHER_CTX_set_padding(ctx, 0);              /* 无 padding：len 须 16 对齐 */
    if (EVP_CipherUpdate(ctx, out, &outl, in, len) != 1) goto done;
    if (EVP_CipherFinal_ex(ctx, out + outl, &tmpl) != 1) goto done;
    rc = outl + tmpl;
done:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}
int nvr_aes256_cbc_enc(const uint8_t k[32], const uint8_t iv[16], const uint8_t *in, int len, uint8_t *out)
{ return aes_run(EVP_aes_256_cbc(), 1, k, iv, in, len, out); }
int nvr_aes256_cbc_dec(const uint8_t k[32], const uint8_t iv[16], const uint8_t *in, int len, uint8_t *out)
{ return aes_run(EVP_aes_256_cbc(), 0, k, iv, in, len, out); }
int nvr_aes256_ecb_enc(const uint8_t k[32], const uint8_t *in, int len, uint8_t *out)
{ return aes_run(EVP_aes_256_ecb(), 1, k, NULL, in, len, out); }
int nvr_aes256_ecb_dec(const uint8_t k[32], const uint8_t *in, int len, uint8_t *out)
{ return aes_run(EVP_aes_256_ecb(), 0, k, NULL, in, len, out); }

/* ==================== 口令派生（NightOwl） ====================
 * ⚠️⚠️ 下面 4 个常量为 NightOwl 私有，**请填真实值**（现为演示/空占位）。
 *   NVR_ENH_KEY_X / NVR_ENH_KEY_Y —— 增强模式 SHA256 前后缀密钥（见 BIND_IPC_FLOW §5）。
 *   NVR_ACT_AES_KEY[32] / NVR_ACT_AES_IV[16] —— 激活密码 AES-256-CBC 的密钥与盐(IV)。
 * 填写后 nvr_pw_algo_ready() 返回 1；未填时增强/激活密码不可用（连接回退空/admin/123456）。 */
#define NVR_ENH_KEY_X ""        /* ← 填 NightOwl 提供的 key[x] */
#define NVR_ENH_KEY_Y ""        /* ← 填 NightOwl 提供的 key[y] */
static const uint8_t NVR_ACT_AES_KEY[32] = {0};   /* ← 填 32 字节激活 AES 密钥 */
static const uint8_t NVR_ACT_AES_IV[16]  = {0};   /* ← 填 16 字节盐(IV) */

/* 取前 n 个「字母数字」字符（对 hex 串即前 n 个字符）。 */
int nvr_first_alnum(const char *in, int n, char *out, size_t out_cap)
{
    if (!in || !out || out_cap < (size_t)n + 1) return -1;
    int k = 0;
    for (const char *p = in; *p && k < n; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            out[k++] = (char)c;
    }
    out[k] = 0;
    return k;
}

/* ① 增强：first16Alnum(SHA256(key_x + random + key_y))（核心可测版） */
int nvr_pw_enhanced_calc(const char *key_x, const char *random, const char *key_y,
                         char *out, size_t out_cap)
{
    if (!random || !out || out_cap < 17) return -1;
    char text[256];
    snprintf(text, sizeof(text), "%s%s%s", key_x ? key_x : "", random, key_y ? key_y : "");
    uint8_t h[NVR_SHA256_LEN]; char hex[NVR_SHA256_LEN * 2 + 1];
    if (nvr_sha256(text, strlen(text), h) != 0) return -1;
    nvr_hex(h, NVR_SHA256_LEN, hex, sizeof(hex));
    return nvr_first_alnum(hex, 16, out, out_cap);
}

int nvr_pw_from_random(const char *random, char *out, size_t out_cap)
{
    return nvr_pw_enhanced_calc(NVR_ENH_KEY_X, random, NVR_ENH_KEY_Y, out, out_cap);
}

/* ② 8012：first16Alnum(MD5(username_ts : "8012" : auth_pwd)) */
int nvr_pw_8012_digest(const char *username_ts, const char *auth_pwd, char *out, size_t out_cap)
{
    if (!username_ts || !auth_pwd || !out || out_cap < 17) return -1;
    char text[128];
    snprintf(text, sizeof(text), "%s:8012:%s", username_ts, auth_pwd);
    uint8_t h[NVR_MD5_LEN]; char hex[NVR_MD5_LEN * 2 + 1];
    if (nvr_md5(text, strlen(text), h) != 0) return -1;
    nvr_hex(h, NVR_MD5_LEN, hex, sizeof(hex));
    return nvr_first_alnum(hex, 16, out, out_cap);
}

/* ③ 激活明文密码 P_act：UPPER(first16(MD5(nvr_sn + dev_sn))) */
int nvr_pw_activate(const char *nvr_sn, const char *dev_sn, char *out, size_t out_cap)
{
    if (!nvr_sn || !dev_sn || !out || out_cap < 17) return -1;
    char text[192];
    snprintf(text, sizeof(text), "%s%s", nvr_sn, dev_sn);
    uint8_t h[NVR_MD5_LEN]; char hex[NVR_MD5_LEN * 2 + 1];
    if (nvr_md5(text, strlen(text), h) != 0) return -1;
    nvr_hex(h, NVR_MD5_LEN, hex, sizeof(hex));
    int k = nvr_first_alnum(hex, 16, out, out_cap);
    for (int i = 0; i < k; i++)
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = (char)(out[i] - 'a' + 'A');  /* 统一大写 HEX */
    return k;
}

/* ④ setDeviceActive 密码字段：hex(AES-256-CBC(P_act, KEY, IV))。P_act=16B 正好一个 block。 */
int nvr_pw_act_encrypt(const char *nvr_sn, const char *dev_sn, char *out, size_t out_cap)
{
    char pact[17];
    if (nvr_pw_activate(nvr_sn, dev_sn, pact, sizeof(pact)) != 16) return -1;   /* 16 字节明文 */
    uint8_t enc[16];
    if (nvr_aes256_cbc_enc(NVR_ACT_AES_KEY, NVR_ACT_AES_IV, (const uint8_t *)pact, 16, enc) != 16)
        return -1;
    if (out_cap < 33) return -1;
    nvr_hex(enc, 16, out, out_cap);        /* 32 hex chars（编码格式如需 base64 可改） */
    return (int)strlen(out);
}

int nvr_pw_algo_ready(void)
{
    int enh_ok = (NVR_ENH_KEY_X[0] != 0) && (NVR_ENH_KEY_Y[0] != 0);
    int act_ok = 0;
    for (int i = 0; i < 32; i++) if (NVR_ACT_AES_KEY[i]) { act_ok = 1; break; }
    return (enh_ok && act_ok) ? 1 : 0;
}
