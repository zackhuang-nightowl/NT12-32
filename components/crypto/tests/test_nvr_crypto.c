/* test_nvr_crypto — 已知测试向量校验 MD5/SHA/AES256 + 口令派生占位可用 */
#include "nvr_crypto.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m) do{ if(!(c)){ printf("FAIL: %s\n", m); g_fail++; } else printf("ok: %s\n", m); }while(0)

static int hexeq(const uint8_t *b, size_t n, const char *hex)
{
    char buf[256]; nvr_hex(b, n, buf, sizeof(buf));
    return strcmp(buf, hex) == 0;
}
static int parse_hex(const char *h, uint8_t *out, int cap)
{
    int n = 0;
    for (; h[0] && h[1] && n < cap; h += 2) {
        unsigned v; sscanf(h, "%2x", &v); out[n++] = (uint8_t)v;
    }
    return n;
}

int main(void)
{
    uint8_t d[32];

    /* MD5("abc") */
    nvr_md5("abc", 3, d);
    CHECK(hexeq(d, NVR_MD5_LEN, "900150983cd24fb0d6963f7d28e17f72"), "MD5(abc)");

    /* SHA1("abc") */
    nvr_sha1("abc", 3, d);
    CHECK(hexeq(d, NVR_SHA1_LEN, "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA1(abc)");

    /* SHA256("abc") */
    nvr_sha256("abc", 3, d);
    CHECK(hexeq(d, NVR_SHA256_LEN, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA256(abc)");

    /* AES-256-ECB FIPS-197 向量: key=000102..1f pt=00112233445566778899aabbccddeeff
       ct=8ea2b7ca516745bfeafc49904b496089 */
    uint8_t key[32], pt[16], ct[16], out[16];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    parse_hex("00112233445566778899aabbccddeeff", pt, 16);
    parse_hex("8ea2b7ca516745bfeafc49904b496089", ct, 16);
    int n = nvr_aes256_ecb_enc(key, pt, 16, out);
    CHECK(n == 16 && memcmp(out, ct, 16) == 0, "AES256-ECB encrypt vector");
    n = nvr_aes256_ecb_dec(key, ct, 16, out);
    CHECK(n == 16 && memcmp(out, pt, 16) == 0, "AES256-ECB decrypt vector");

    /* AES-256-CBC roundtrip */
    uint8_t iv[16]; memset(iv, 7, 16);
    uint8_t enc[16], dec[16];
    CHECK(nvr_aes256_cbc_enc(key, iv, pt, 16, enc) == 16, "AES256-CBC enc");
    CHECK(nvr_aes256_cbc_dec(key, iv, enc, 16, dec) == 16 && memcmp(dec, pt, 16) == 0, "AES256-CBC roundtrip");

    /* ① 增强密码 —— 用 BIND_IPC_FLOW §5 文档演示向量校验核心算法 */
    char pw[128];
    nvr_pw_enhanced_calc("eT79Uo51sK", "sfa8a9", "SzxGJDZxjJ", pw, sizeof(pw));
    CHECK(strcmp(pw, "7f9c00a7f43d4716") == 0, "P_enh 文档向量");

    /* ② 8012 增强 digest —— 文档向量 */
    nvr_pw_8012_digest("1758163048", "7f9c00a7f43d4716", pw, sizeof(pw));
    CHECK(strcmp(pw, "e51cd5478b87be52") == 0, "P_8012 文档向量");

    /* ③ 激活明文 P_act —— UPPER(first16(MD5(nvr_sn+dev_sn)))，确定性 */
    nvr_pw_activate("NVRSN001", "IPCSN002", pw, sizeof(pw));
    CHECK((int)strlen(pw) == 16, "P_act 长度 16");
    { int up = 1; for (int i = 0; pw[i]; i++) if (pw[i] >= 'a' && pw[i] <= 'z') up = 0;
      CHECK(up, "P_act 大写 HEX"); }
    char pw2[128]; nvr_pw_activate("NVRSN001", "IPCSN002", pw2, sizeof(pw2));
    CHECK(strcmp(pw, pw2) == 0, "P_act 确定性");

    /* first_alnum 工具 */
    nvr_first_alnum("ab-cd:ef!gh12345", 8, pw, sizeof(pw));
    CHECK(strcmp(pw, "abcdefgh") == 0, "first_alnum 过滤非字母数字");

    /* 密钥未填 → algo_ready==0（提醒填 KEY_X/KEY_Y/AES 密钥） */
    CHECK(nvr_pw_algo_ready() == 0, "pw_algo_ready==0 (密钥未填)");

    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
