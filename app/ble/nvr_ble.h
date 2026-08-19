/***************************************************************************************
 *  nvr_ble.h — BLE 通路（配网/锁定/查设，不走音视频流）
 *
 *  设计要点（见 nop_doc/videoRecorder/BLE/videoRecorder_BLE通讯与控制.md）：
 *    · GATT Server：Service 0xFFF0 / RX 0xFFF1(APP write) / TX 0xFFF2(NVR notify)
 *    · APP 通过 BLE 发送的就是**同一套 NOP JSON**（X_NightOwl_loginUser{BLEKey}/setOwner/
 *      setName/setTimezone…），因此本模块只做「组包→交给 nvr_cmd_dispatch→回复分包」，
 *      业务零重复——与 8089 HTTP 复用同一命令路由。
 *    · 广播：Device Name = NO_<model>；Manufacturer Specific = <MAC无冒号>_<SN>[_<lockbit>]
 *      （未绑定=无 lockbit；已绑定锁定=_0；已绑定解锁=_1）；间隔 200ms。
 *
 *  分层：本模块是**纯逻辑 + 可插拔链路**——分包/组包/派发可在主机单测；真正的射频/GATT
 *  由 nvr_ble_link_t 回调接入（真机上接 BlueZ GATT server 或厂商 BT 模组，属板级依赖）。
 *  MTU≥40：首包 6B header + ≤34B 数据；中/尾包 1B header + ≤39B 数据。
 ***************************************************************************************/
#ifndef NVR_BLE_H
#define NVR_BLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 可插拔链路：由板级 BLE 栈实现并注入 ---- */
typedef struct {
    void *ud;                                            /* 板级上下文 */
    /* 经 0xFFF2 Notify 一个 BLE 包（≤MTU）。返回 0 成功。 */
    int (*notify)(void *ud, const uint8_t *pkt, int len);
    /* 更新广播内容（Device Name + Manufacturer Specific）。可为 NULL。 */
    void (*set_adv)(void *ud, const char *name, const uint8_t *mfg, int mfg_len);
} nvr_ble_link_t;

/* ---- 命令桥：组包完成的一整条 NOP JSON → 应答(malloc；BLE 内部 free)。 ---- */
typedef char *(*nvr_ble_dispatch_fn)(void *ud, const char *json, int enc);

typedef struct {
    nvr_ble_link_t      link;          /* 板级链路（notify/set_adv） */
    nvr_ble_dispatch_fn dispatch;      /* 桥到 nvr_cmd_dispatch */
    void               *dispatch_ud;   /* 传给 dispatch 的上下文（router 句柄） */
    char                model[48];     /* 广播设备名 NO_<model> */
    char                mac[24];       /* "54:2b:57:.." 或无冒号 */
    char                serial[40];    /* SN */
    char                crypt_key[32]; /* AES 密码：有 ble.key 用它，否则 MAC_SN 前 16 */
    int                 mtu;           /* 协商 MTU（默认 40） */
} nvr_ble_cfg_t;

typedef struct nvr_ble nvr_ble_t;

int  nvr_ble_create (const nvr_ble_cfg_t *cfg, nvr_ble_t **out);
void nvr_ble_destroy(nvr_ble_t *b);

/* 板级链路收到 0xFFF1 写入的一个 BLE 包时调用（驱动组包；完整后自动派发+分包回复）。返回 0。 */
int  nvr_ble_on_rx  (nvr_ble_t *b, const uint8_t *pkt, int len);

/* 绑定/锁定状态变化时刷新广播（bound=是否已绑定账户；unlocked=本地是否已登录解锁）。 */
void nvr_ble_update_adv(nvr_ble_t *b, int bound, int unlocked);

/* 热更新 AES 密码（新连接生效；当前连接保持不变时由上层勿在会话中途调用）。空串则回落 MAC_SN 前 16。 */
void nvr_ble_set_crypt_key(nvr_ble_t *b, const char *key);

/* 生成 16 位十六进制 BLEKey（对齐文档例 0F8BB4A05C92A8F3），写入 out（≥17）。成功返回 0。 */
int  nvr_ble_gen_key16(char *out, int out_cap);

/* 解析当前应使用的 AES 密码：优先 ble_key；否则 MAC无冒号_SN 前 16 字符。 */
void nvr_ble_resolve_crypt_key(const char *ble_key, const char *mac, const char *serial,
                               char *out, int out_cap);

/* ---- 分包/组包底层（导出供单测；真机链路一般不直接用） ---- */
/* 把 payload 按 MTU 切成若干 BLE 包，逐个回调 emit。enc: 0 明文 / 1 加密。返回包数或<0。 */
int  nvr_ble_frame_encode(const uint8_t *payload, int len, int enc, int mtu,
                          int (*emit)(void *ud, const uint8_t *pkt, int n), void *ud);

#ifdef __cplusplus
}
#endif
#endif /* NVR_BLE_H */
