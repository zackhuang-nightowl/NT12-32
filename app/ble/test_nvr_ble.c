/* test_nvr_ble.c — BLE 分包/组包 round-trip + 派发桥接（主机单测，不需 BT 硬件）
 * 注意：用 CHECK 而非 assert——assert 在 NDEBUG 下会把有副作用的调用一起删掉。 */
#include "nvr_ble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(cond) do { if (!(cond)) { \
    printf("  FAIL: %s (line %d)\n", #cond, __LINE__); exit(1); } } while (0)

/* 收集 encode 产生的包，再逐个喂给 on_rx 组回，验证 dispatch 收到的整条一致 */
static char g_seen[8192]; static int g_enc_seen = -1;
static char g_last_notify[8192]; static int g_notify_total;

/* dispatch：回显收到的 JSON（大写化以区分请求/应答） */
static char *echo_dispatch(void *ud, const char *json, int enc)
{
    (void)ud;
    strncpy(g_seen, json, sizeof(g_seen) - 1);
    g_enc_seen = enc;
    char *r = malloc(strlen(json) + 32);
    sprintf(r, "{\"echo\":\"%zu\"}", strlen(json));
    return r;
}

/* notify：把应答包重组到 g_last_notify（复用同一组包逻辑做逆向校验） */
static int cap_notify(void *ud, const uint8_t *pkt, int len)
{
    (void)ud;
    if (pkt[0] == 0x01) {           /* 首包 */
        g_notify_total = pkt[1] | (pkt[2] << 8);
        memcpy(g_last_notify, pkt + 6, len - 6);
        g_last_notify[len - 6] = 0;
    }
    return 0;
}

/* 收集 encode 输出的包指针 */
#define MAXP 512
static uint8_t g_pkts[MAXP][64]; static int g_plen[MAXP]; static int g_np;
static int collect(void *ud, const uint8_t *pkt, int n) { (void)ud;
    memcpy(g_pkts[g_np], pkt, n); g_plen[g_np] = n; g_np++; return 0; }

static void roundtrip(const char *msg, int enc)
{
    nvr_ble_cfg_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.dispatch = echo_dispatch; cfg.mtu = 40;
    cfg.link.notify = cap_notify;
    strcpy(cfg.model, "NVR-BTN8-8"); strcpy(cfg.mac, "54:2b:57:3e:93:dc"); strcpy(cfg.serial, "GPHER35X4DI6");
    nvr_ble_t *b; assert(nvr_ble_create(&cfg, &b) == 0);

    g_np = 0; g_seen[0] = 0; g_enc_seen = -1;
    int np = nvr_ble_frame_encode((const uint8_t *)msg, (int)strlen(msg), enc, 40, collect, NULL);
    assert(np > 0);
    /* 首包头校验 */
    assert(g_pkts[0][0] == 0x01);
    assert((g_pkts[0][1] | (g_pkts[0][2] << 8)) == (int)strlen(msg));
    assert(g_pkts[0][5] == (enc ? 1 : 0));
    /* 逐包喂 on_rx 组回 */
    for (int i = 0; i < g_np; i++) assert(nvr_ble_on_rx(b, g_pkts[i], g_plen[i]) == 0);
    assert(strcmp(g_seen, msg) == 0);       /* dispatch 收到完整原文 */
    assert(g_enc_seen == enc);              /* enc 位透传 */
    nvr_ble_destroy(b);
    printf("  ok: len=%zu enc=%d packets=%d\n", strlen(msg), enc, np);
}

int main(void)
{
    printf("test_nvr_ble:\n");
    roundtrip("{\"func\":\"getLiveCapabilities\"}", 0);                 /* 单首包 30B */
    roundtrip("{\"func\":\"setName\",\"args\":{\"name\":\"myDVR\"}}", 0);/* 首+尾 42B */
    roundtrip("{\"func\":\"X_NightOwl_loginUser\",\"args\":{\"user\":\"admin\",\"BLEKey\":\"admin\"}}", 1); /* 首+中+尾 */
    /* 大响应（>255B）验证 2 字节长度 */
    char big[900]; memset(big, 'x', sizeof(big)); big[0] = '{'; big[sizeof(big)-2] = '}'; big[sizeof(big)-1] = 0;
    roundtrip(big, 1);
    printf("ALL PASS\n");
    return 0;
}
