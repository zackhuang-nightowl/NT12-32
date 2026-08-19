/***************************************************************************************
 *  nvr_crypto.c — MD5/SHA/AES-256 封装（OpenSSL EVP）+ 口令派生占位。见 nvr_crypto.h。
 ***************************************************************************************/
#include "nvr_crypto.h"

#include <openssl/evp.h>
#include <stdlib.h>
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

/* ---------------- BLE AES-128-ECB ---------------- */
static void ble_key16(const char *password, uint8_t key[16])
{
    memset(key, 0, 16);
    if (!password) return;
    size_t n = strlen(password);
    if (n > 16) n = 16;
    memcpy(key, password, n);
}

static int aes128_ecb(int enc, const uint8_t key[16], const uint8_t *in, int len, uint8_t *out)
{
    if (!in || !out || len <= 0 || (len % 16) != 0) return -1;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    int rc = -1, outl = 0, tmpl = 0;
    /* IV 同 key（与 BLE_helper 一致；ECB 实际忽略 IV，但 CreateEncryptor 会设） */
    if (EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, key, enc) != 1) goto done;
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    if (EVP_CipherUpdate(ctx, out, &outl, in, len) != 1) goto done;
    if (EVP_CipherFinal_ex(ctx, out + outl, &tmpl) != 1) goto done;
    rc = outl + tmpl;
done:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

int nvr_ble_aes_enc(const char *password, const uint8_t *in, int in_len,
                    uint8_t *out, int out_cap)
{
    if (!password || !in || in_len < 0 || !out) return -1;
    int pad = (16 - (in_len % 16)) % 16;
    int total = in_len + pad;
    if (total > out_cap) return -1;
    uint8_t key[16];
    ble_key16(password, key);
    if (pad) {
        /* 需临时缓冲（in 可能不可写） */
        uint8_t *tmp = (uint8_t *)malloc((size_t)total);
        if (!tmp) return -1;
        memcpy(tmp, in, (size_t)in_len);
        memset(tmp + in_len, 0, (size_t)pad);
        int rc = aes128_ecb(1, key, tmp, total, out);
        free(tmp);
        return rc;
    }
    return aes128_ecb(1, key, in, in_len, out);
}

int nvr_ble_aes_dec(const char *password, const uint8_t *in, int in_len,
                    uint8_t *out, int out_cap)
{
    if (!password || !in || in_len < 0 || !out) return -1;
    int pad = (16 - (in_len % 16)) % 16;
    int total = in_len + pad;
    if (total > out_cap) return -1;
    uint8_t key[16];
    ble_key16(password, key);
    if (pad) {
        uint8_t *tmp = (uint8_t *)malloc((size_t)total);
        if (!tmp) return -1;
        memcpy(tmp, in, (size_t)in_len);
        memset(tmp + in_len, 0, (size_t)pad);
        int rc = aes128_ecb(0, key, tmp, total, out);
        free(tmp);
        return rc;
    }
    return aes128_ecb(0, key, in, in_len, out);
}

/* ==================== 口令派生（NightOwl） ====================
 * ⚠️⚠️ 下面 4 个常量为 NightOwl 私有，**请填真实值**（现为演示/空占位）。
 *   NVR_ENH_KEY_X / NVR_ENH_KEY_Y —— 增强模式 SHA256 前后缀密钥（见 BIND_IPC_FLOW §5）。
 *   NVR_ACT_AES_KEY[32] —— 激活密码 AES-256-ECB 密钥（ASCII 后补 0）。 */
/* BIND_IPC_FLOW §5 文档向量（可被 nvr_pw_set_keys / 设置库 factory.* 覆盖）。 */
#define NVR_ENH_KEY_X "eT79Uo51sK"
#define NVR_ENH_KEY_Y "SzxGJDZxjJ"
/* key = ASCII eT79Uo51sK + 22 个 0，凑满 32 字节。只要 key，ECB 不要 IV。 */
static const uint8_t NVR_ACT_AES_KEY[32] = {
    'e','T','7','9','U','o','5','1','s','K'
};

static char    g_key_x[64] = NVR_ENH_KEY_X;
static char    g_key_y[64] = NVR_ENH_KEY_Y;
static uint8_t g_aes_key[32];
static int     g_aes_set;

static int parse_hex(const char *hex, uint8_t *out, int nbytes)
{
    if (!hex || !out) return -1;
    int n = 0;
    for (const char *p = hex; *p && n < nbytes; ) {
        while (*p == ' ' || *p == ':') p++;
        if (!p[0] || !p[1]) break;
        unsigned v = 0;
        if (sscanf(p, "%2x", &v) != 1) return -1;
        out[n++] = (uint8_t)v;
        p += 2;
    }
    return n == nbytes ? 0 : -1;
}

int nvr_pw_set_keys(const char *key_x, const char *key_y,
                    const char *aes_key_hex, const char *aes_iv_hex)
{
    if (key_x && key_x[0]) snprintf(g_key_x, sizeof(g_key_x), "%s", key_x);
    if (key_y && key_y[0]) snprintf(g_key_y, sizeof(g_key_y), "%s", key_y);
    (void)aes_iv_hex;   /* ECB 不用 IV */
    if (aes_key_hex && aes_key_hex[0]) {
        if (parse_hex(aes_key_hex, g_aes_key, 32) == 0)
            g_aes_set = 1;
        else
            return -1;
    }
    return 0;
}

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
    return nvr_pw_enhanced_calc(g_key_x, random, g_key_y, out, out_cap);
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

/* ④ setDeviceActive 密码字段：hex(AES-256-ECB(P_act, KEY))。P_act=16B 正好一个 block。 */
int nvr_pw_act_encrypt(const char *nvr_sn, const char *dev_sn, char *out, size_t out_cap)
{
    char pact[17];
    if (nvr_pw_activate(nvr_sn, dev_sn, pact, sizeof(pact)) != 16) return -1;   /* 16 字节明文 */
    uint8_t enc[16];
    const uint8_t *key = g_aes_set ? g_aes_key : NVR_ACT_AES_KEY;
    if (nvr_aes256_ecb_enc(key, (const uint8_t *)pact, 16, enc) != 16)
        return -1;
    if (out_cap < 33) return -1;
    nvr_hex(enc, 16, out, out_cap);
    return (int)strlen(out);
}

int nvr_pw_enh_ready(void)
{
    return (g_key_x[0] && g_key_y[0]) ? 1 : 0;
}

int nvr_pw_act_ready(void)
{
    if (g_aes_set) return 1;
    for (int i = 0; i < 32; i++) if (NVR_ACT_AES_KEY[i]) return 1;
    return 0;
}

int nvr_pw_algo_ready(void)
{
    return nvr_pw_enh_ready();
}
