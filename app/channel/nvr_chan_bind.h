/***************************************************************************************
 *  nvr_chan_bind.h — BIND_IPC 握手：用户密码优先，失败再静默试增强 / 激活 / 无鉴权
 *
 *  NOP digest（增强安全）开关不在连接路径里打开：GUI_setLanDevice.enhancedSecurity
 *  或 setEnhancedSecurity 才 SET random。连接时若设备已开 digest，GET random → P_enh。
 ***************************************************************************************/
#ifndef NVR_CHAN_BIND_H
#define NVR_CHAN_BIND_H

#include "nvr_config.h"
#include "nvr_chan_status.h"
#include "nvr_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_AUTH_CRED_MAX 4

typedef struct {
    char user[64];
    char pass[64];   /* 空 = 无鉴权（不带 digest） */
} nvr_auth_cred_t;

/** 组装候选账密：用户输入（非 123456）优先，再 P_enh、P_act、无鉴权。
 *  admin/123456 视为无鉴权，不单独占一项。返回候选数。
 *  P_enh 仅设备已开 digest（GET random 非空）才加入，连接路径不 SET。
 *  P_act 仅 kind==NOPONVIF。 */
int nvr_chan_auth_candidates(nvr_channel_t *d, nvr_settings_t *st,
                             nvr_chan_substate_t *sub,
                             nvr_auth_cred_t *out, int cap);

/** GET 设备增强 random。0=成功（空串=digest 关）；1=501 不支持；<0 失败。
 *  401+Random / 402 会改 d 的 user/pass/enh_random。 */
int nvr_chan_enh_get(nvr_channel_t *d, char *random_out, size_t cap);

/** 开/关 NOP digest。enable=1：已开则沿用 random，否则 NVR 生成 random 并 SET，算出 P_enh。
 *  enable=0：SET random=""（关 digest）。set_random 非空时强制用该 random。
 *  成功时写入 d->enh_random / d->user / d->pass。设备 channel 用 d->dev_chn。 */
int nvr_chan_enh_apply(nvr_channel_t *d, int enable, const char *set_random,
                       char *penh_out, size_t cap);

/** 鉴权失败自愈：GET 设备。501/random 空 → 清本地回普通模式(返 1)；
 *  random 非空 → 更新 P_enh(返 0)。可重试取流。<0 无法恢复。 */
int nvr_chan_enh_on_auth_fail(nvr_channel_t *d);

/** POST /APPJsonCmd。401+Random 用新 P_enh 重试；402 清 digest 再以无鉴权重试。
 *  可能改写 d 的账密/random。返回 malloc 应答，失败 NULL。 */
char *nvr_chan_bind_post(nvr_channel_t *d, const char *json_in);

/** nopOnvif 激活：GET → 未激活则 SET AES 密文 → 再 GET 确认。
 *  设备侧 channel 固定 1（协议设备级，不是 NVR dev_chn）。不带 digest。
 *  0=已确认激活（pact_out 为明文 P_act）；1=501 不支持激活；<0 失败仍未激活。 */
int nvr_chan_try_activate(nvr_channel_t *d, nvr_chan_substate_t *sub,
                          char *pact_out, size_t cap);

/** 只查询 getDeviceActive。返 200/501；失败 -1。status_out：0 未激活 / 1 已激活。 */
int nvr_chan_get_device_active(const nvr_channel_t *d, int *status_out);

#ifdef __cplusplus
}
#endif
#endif
