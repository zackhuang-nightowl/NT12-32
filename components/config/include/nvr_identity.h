/***************************************************************************************
 *  nvr_identity.h — 设备身份读写（数据分区文件为权威源，对齐 ODC libDVRAPI 模型）
 *
 *  设计（计划：身份读取改造）：
 *    - 身份权威源是数据分区文件（/User、/SYS，ubifs，OTA 不覆盖），不再走 settings 库/JSON 种子。
 *    - SN / MAC 出厂后恒定,跟随物理设备,只读:
 *        SN       ← /User/OWLSerialNumber
 *        MAC eth0 ← /User/mac_addr_v2        (缺失回退 /sys/class/net/eth0/address)
 *        MAC eth1 ← /User/mac_addr_v2.eth1   (缺失回退 /sys/class/net/eth1/address)
 *    - UID / TUTK 凭据通过封装接口可配置(读+写回文件持久化):
 *        UID      ↔ /User/tutk_agent_udid
 *        IOTCKey  ↔ /User/OWL/tutkdata.json : "IotcAuthKey"  (缺省 00000000)
 *        AVKey    ↔ /User/OWL/tutkdata.json : "AvPassword"   (缺省 888888)
 *        MODEL    ↔ /User/OWLModel                            (缺省 NOP12-32)
 *
 *  路径基目录可编译期覆盖(NVR_USER_DIR / NVR_SYS_DIR)供 host 单测重定向。
 *  本组件为叶子:仅依赖 cJSON,不反向依赖 app/。
 ***************************************************************************************/
#ifndef NVR_IDENTITY_H
#define NVR_IDENTITY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 数据分区缺省值(文件缺失/为空时回退),对齐 NOP 平台默认凭据。 */
#define NVR_IDENTITY_DEF_IOTCKEY "00000000"   /* 出厂默认 IotcAuthKey(APP_client_Agent.md) */
#define NVR_IDENTITY_DEF_AVKEY   "888888"
#define NVR_IDENTITY_DEF_AVACCOUNT "admin"    /* 出厂默认 AvAccount(AV 登录账户) */
#define NVR_IDENTITY_DEF_MODEL   "NOP12-32"   /* 机型缺省(/User/OWLModel 缺失时回退) */

/* ---------- 恒定身份(只读,跟随物理设备) ----------
 * 成功返回写入长度(不含结尾 0)。文件缺失/空:
 *   get_sn  → out[0]=0 返回 0
 *   get_mac → 回退 /sys/class/net/<iface>/address;仍无则 out[0]=0 返回 0
 * iface 传 "eth0" / "eth1"(NULL 视作 "eth0")。 */
int nvr_identity_get_sn(char *out, size_t cap);
int nvr_identity_get_mac(const char *iface, char *out, size_t cap);

/* ---------- 可配置身份(读/写,写回数据分区文件持久化) ----------
 * get_*  成功返回写入长度(不含 0),缺失返回缺省(见上);<0 = 参数错。
 * set_*  原子写(tmp+rename)成功返回 0,失败 <0。 */
int nvr_identity_get_uid(char *out, size_t cap);
int nvr_identity_set_uid(const char *uid);

/* MODEL(机型):存 /User/OWLModel。get 缺失/空 → 回退缺省 NOP12-32 并返回其长度;
 * set 原子写成功 0,失败 <0。是 DHCP 主机名(Option 12)与 setDeviceInfo 的权威源。 */
int nvr_identity_get_model(char *out, size_t cap);
int nvr_identity_set_model(const char *model);

/* IOTCKey/AVKey 同源单文件 tutkdata.json;任一 out 可传 NULL 表示不取。 */
int nvr_identity_get_tutk_creds(char *iotckey, size_t kc, char *avkey, size_t ac);
/* iotckey/avkey 任一可传 NULL 表示保持文件内该字段不变(仅改另一个)。 */
int nvr_identity_set_tutk_creds(const char *iotckey, const char *avkey);
/* AvAccount(AV 登录账户)← tutkdata.json:"AvAccount",缺省 "admin"。与 iotc/av 同源、全由 json 控。 */
int nvr_identity_get_av_account(char *out, size_t cap);

/* 供 set_* 后使 SN/MAC 进程内缓存失效(通常无需;SN/MAC 恒定不写)。 */
void nvr_identity_cache_invalidate(void);

/* 首启兜底 provisioning:tutkdata.json 缺失则用缺省(00000000/888888)生成一份;
 * 并把 SN/MAC/UID 当前状态打日志(UID/SN 为空会 WARN,提示待产测/注册写入)。启动时调用一次。 */
void nvr_identity_ensure_provisioned(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_IDENTITY_H */
