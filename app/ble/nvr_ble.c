/***************************************************************************************
 *  nvr_ble.c — BLE 通路实现。见 nvr_ble.h。
 *
 *  包结构（首包 6B header，中/尾包 1B header；MTU 默认 40）：
 *    首包 : [0x01][lenLo][lenHi][0][0][enc]  + 数据(≤MTU-6)   lenLo/Hi=整条数据长度(LE16)
 *    中包 : [0x02]                            + 数据(≤MTU-1)
 *    尾包 : [0x00]                            + 数据(≤MTU-1)
 *    若整条数据 ≤ MTU-6，只发首包（首包即含全部数据，无尾包）。
 *  与 nop_doc 例子逐字节一致（enc 位在首包第 6 字节，0 明文/1 加密）。
 ***************************************************************************************/
#include "nvr_ble.h"
#include "nvr_crypto.h"
#include "nvr_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define BLE_PKT_FIRST  0x01
#define BLE_PKT_MID    0x02
#define BLE_PKT_LAST   0x00
#define BLE_FIRST_HDR  6
#define BLE_CONT_HDR   1
#define BLE_MAX_MSG    (16*1024)   /* 组包上限，防恶意超长 */

struct nvr_ble {
    nvr_ble_cfg_t cfg;
    /* 组包状态 */
    uint8_t  *asm_buf;
    int       asm_cap;
    int       asm_len;     /* 已收字节 */
    int       asm_total;   /* 期望总长（首包给出） */
    int       asm_enc;     /* 本条是否加密 */
    int       asm_active;  /* 是否正在组一条 */
};

void nvr_ble_resolve_crypt_key(const char *ble_key, const char *mac, const char *serial,
                               char *out, int out_cap)
{
    if (!out || out_cap < 2) return;
    out[0] = 0;
    if (ble_key && ble_key[0]) {
        snprintf(out, (size_t)out_cap, "%s", ble_key);
        return;
    }
    /* 无 Blekey：广播 MAC_SN 前 16（与 BLE_helper 一致） */
    char mac_nocolon[24] = {0};
    int j = 0;
    for (const char *p = mac ? mac : ""; *p && j < (int)sizeof(mac_nocolon) - 1; p++)
        if (*p != ':' && *p != '-') mac_nocolon[j++] = (char)toupper((unsigned char)*p);
    char mfg[80];
    snprintf(mfg, sizeof(mfg), "%s_%s", mac_nocolon, serial ? serial : "");
    snprintf(out, (size_t)out_cap, "%.16s", mfg);
}

int nvr_ble_gen_key16(char *out, int out_cap)
{
    if (!out || out_cap < 17) return -1;
    unsigned char rnd[8];
    int filled = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, rnd, sizeof(rnd));
        close(fd);
        if (n == (ssize_t)sizeof(rnd)) filled = 1;
    }
    if (!filled) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (int i = 0; i < 8; i++) rnd[i] = (unsigned char)(rand() & 0xFF);
    }
    static const char *H = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = H[(rnd[i] >> 4) & 0xF];
        out[i * 2 + 1] = H[rnd[i] & 0xF];
    }
    out[16] = 0;
    return 0;
}

void nvr_ble_set_crypt_key(nvr_ble_t *b, const char *key)
{
    if (!b) return;
    if (key && key[0])
        snprintf(b->cfg.crypt_key, sizeof(b->cfg.crypt_key), "%s", key);
    else
        nvr_ble_resolve_crypt_key(NULL, b->cfg.mac, b->cfg.serial,
                                  b->cfg.crypt_key, (int)sizeof(b->cfg.crypt_key));
    NVR_LOGI("ble", "crypt_key 已更新 len=%d", (int)strlen(b->cfg.crypt_key));
}

/* ---------------- 分包（encode） ---------------- */
int nvr_ble_frame_encode(const uint8_t *payload, int len, int enc, int mtu,
                         int (*emit)(void *ud, const uint8_t *pkt, int n), void *ud)
{
    if (!payload || len < 0 || !emit) return -1;
    if (mtu < 7) mtu = 40;
    uint8_t pkt[512];
    if (mtu > (int)sizeof(pkt)) mtu = sizeof(pkt);

    int first_cap = mtu - BLE_FIRST_HDR;   /* 首包数据容量 */
    int cont_cap  = mtu - BLE_CONT_HDR;    /* 中/尾包数据容量 */
    int off = 0, npkt = 0;

    /* 首包 */
    int n = (len < first_cap) ? len : first_cap;
    pkt[0] = BLE_PKT_FIRST;
    pkt[1] = (uint8_t)(len & 0xFF);
    pkt[2] = (uint8_t)((len >> 8) & 0xFF);
    pkt[3] = 0; pkt[4] = 0;
    pkt[5] = (uint8_t)(enc ? 1 : 0);
    memcpy(pkt + BLE_FIRST_HDR, payload, n);
    if (emit(ud, pkt, BLE_FIRST_HDR + n) != 0) return -1;
    npkt++; off += n;

    /* 中/尾包 */
    while (off < len) {
        n = (len - off < cont_cap) ? (len - off) : cont_cap;
        int last = (off + n >= len);
        pkt[0] = last ? BLE_PKT_LAST : BLE_PKT_MID;
        memcpy(pkt + BLE_CONT_HDR, payload + off, n);
        if (emit(ud, pkt, BLE_CONT_HDR + n) != 0) return -1;
        npkt++; off += n;
    }
    return npkt;
}

/* ---------------- 组包 + 派发 ---------------- */
static int asm_reserve(struct nvr_ble *b, int need)
{
    if (need <= b->asm_cap) return 0;
    int cap = b->asm_cap ? b->asm_cap : 256;
    while (cap < need) cap *= 2;
    uint8_t *p = realloc(b->asm_buf, cap);
    if (!p) return -1;
    b->asm_buf = p; b->asm_cap = cap;
    return 0;
}

static void asm_reset(struct nvr_ble *b)
{
    b->asm_len = b->asm_total = b->asm_enc = b->asm_active = 0;
}

/* notify 一个 BLE 包的适配（供 frame_encode 的 emit） */
static int emit_notify(void *ud, const uint8_t *pkt, int n)
{
    struct nvr_ble *b = (struct nvr_ble *)ud;
    if (!b->cfg.link.notify) return -1;
    return b->cfg.link.notify(b->cfg.link.ud, pkt, n);
}

/* 组好一整条 → 解密(可选) → 派发 → 应答加密(可选) → 分包 notify */
static void deliver(struct nvr_ble *b)
{
    const uint8_t *payload = b->asm_buf;
    int payload_len = b->asm_len;
    uint8_t *dec_buf = NULL;
    int enc = b->asm_enc;

    if (enc) {
        int cap = ((b->asm_len + 15) & ~15) + 16;
        dec_buf = (uint8_t *)malloc((size_t)cap + 1);
        if (!dec_buf) { asm_reset(b); return; }
        int n = nvr_ble_aes_dec(b->cfg.crypt_key, b->asm_buf, b->asm_len, dec_buf, cap);
        if (n < 0) {
            NVR_LOGW("ble", "AES 解密失败（crypt_key len=%d）", (int)strlen(b->cfg.crypt_key));
            free(dec_buf);
            asm_reset(b);
            return;
        }
        /* 去掉 zero-pad 尾零，便于当 JSON C 串 */
        while (n > 0 && dec_buf[n - 1] == 0) n--;
        dec_buf[n] = 0;
        payload = dec_buf;
        payload_len = n;
    } else {
        if (asm_reserve(b, b->asm_len + 1) == 0) b->asm_buf[b->asm_len] = 0;
        payload = b->asm_buf;
        payload_len = b->asm_len;
    }

    NVR_LOGI("ble", "组包完成 %d 字节 (enc=%d) → dispatch", payload_len, enc);

    char *resp = NULL;
    if (b->cfg.dispatch)
        resp = b->cfg.dispatch(b->cfg.dispatch_ud, (const char *)payload, enc);
    if (!resp) resp = strdup("{\"statusCode\":500,\"statusMsg\":\"no dispatch\"}");
    free(dec_buf);

    const uint8_t *out_payload = (const uint8_t *)resp;
    int out_len = (int)strlen(resp);
    uint8_t *enc_buf = NULL;
    if (enc) {
        int cap = ((out_len + 15) & ~15) + 16;
        enc_buf = (uint8_t *)malloc((size_t)cap);
        if (enc_buf) {
            int n = nvr_ble_aes_enc(b->cfg.crypt_key, (const uint8_t *)resp, out_len, enc_buf, cap);
            if (n > 0) { out_payload = enc_buf; out_len = n; }
            else { free(enc_buf); enc_buf = NULL; enc = 0; }
        } else {
            enc = 0;
        }
    }

    nvr_ble_frame_encode(out_payload, out_len, enc, b->cfg.mtu, emit_notify, b);
    free(enc_buf);
    free(resp);
    asm_reset(b);
}

int nvr_ble_on_rx(nvr_ble_t *b, const uint8_t *pkt, int len)
{
    if (!b || !pkt || len < 1) return -1;
    uint8_t type = pkt[0];

    if (type == BLE_PKT_FIRST) {
        if (len < BLE_FIRST_HDR) return -1;
        int total = pkt[1] | (pkt[2] << 8);
        if (total < 0 || total > BLE_MAX_MSG) { asm_reset(b); return -1; }
        asm_reset(b);
        b->asm_active = 1;
        b->asm_total  = total;
        b->asm_enc    = pkt[5] ? 1 : 0;
        int n = len - BLE_FIRST_HDR;
        if (n > 0) {
            if (asm_reserve(b, n) != 0) { asm_reset(b); return -1; }
            memcpy(b->asm_buf, pkt + BLE_FIRST_HDR, n);
            b->asm_len = n;
        }
    } else if (type == BLE_PKT_MID || type == BLE_PKT_LAST) {
        if (!b->asm_active) return -1;           /* 无首包，丢弃 */
        int n = len - BLE_CONT_HDR;
        if (n > 0) {
            if (asm_reserve(b, b->asm_len + n) != 0) { asm_reset(b); return -1; }
            memcpy(b->asm_buf + b->asm_len, pkt + BLE_CONT_HDR, n);
            b->asm_len += n;
        }
    } else {
        return -1;
    }

    /* 收满（首包即含全部，或尾包补齐） */
    if (b->asm_active && b->asm_len >= b->asm_total) {
        b->asm_len = b->asm_total;               /* 截去多余 */
        deliver(b);
    }
    return 0;
}

/* ---------------- 广播 ---------------- */
void nvr_ble_update_adv(nvr_ble_t *b, int bound, int unlocked)
{
    if (!b) return;
    /* Device Name = NO_<model> */
    char name[64];
    snprintf(name, sizeof(name), "NO_%s", b->cfg.model);

    /* Manufacturer Specific = <MAC无冒号>_<SN>[_<lockbit>] */
    char mac_nocolon[24] = {0};
    int j = 0;
    for (const char *p = b->cfg.mac; *p && j < (int)sizeof(mac_nocolon) - 1; p++)
        if (*p != ':' && *p != '-') mac_nocolon[j++] = (char)toupper((unsigned char)*p);

    char mfg[80];
    if (!bound)
        snprintf(mfg, sizeof(mfg), "%s_%s", mac_nocolon, b->cfg.serial);
    else
        snprintf(mfg, sizeof(mfg), "%s_%s_%d", mac_nocolon, b->cfg.serial, unlocked ? 1 : 0);

    NVR_LOGI("ble", "广播: name=%s mfg=%s", name, mfg);
    if (b->cfg.link.set_adv)
        b->cfg.link.set_adv(b->cfg.link.ud, name, (const uint8_t *)mfg, (int)strlen(mfg));
}

/* ---------------- 生命周期 ---------------- */
int nvr_ble_create(const nvr_ble_cfg_t *cfg, nvr_ble_t **out)
{
    if (!cfg || !out) return -1;
    struct nvr_ble *b = calloc(1, sizeof(*b));
    if (!b) return -1;
    b->cfg = *cfg;
    if (b->cfg.mtu < 7) b->cfg.mtu = 40;
    if (!b->cfg.crypt_key[0])
        nvr_ble_resolve_crypt_key(NULL, b->cfg.mac, b->cfg.serial,
                                  b->cfg.crypt_key, (int)sizeof(b->cfg.crypt_key));
    *out = b;
    NVR_LOGI("ble", "BLE 通路就绪 (model=%s mtu=%d crypt_len=%d, 链路%s)",
             b->cfg.model, b->cfg.mtu, (int)strlen(b->cfg.crypt_key),
             cfg->link.notify ? "已接" : "未接(stub)");
    return 0;
}

void nvr_ble_destroy(nvr_ble_t *b)
{
    if (!b) return;
    free(b->asm_buf);
    free(b);
}
