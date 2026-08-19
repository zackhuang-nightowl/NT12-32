/***************************************************************************************
 *  nvr_ota.h — NVR 固件自升级（下载/本地 .rom → MD5+版本校验 → A/B 烧写）
 *
 *  包格式（签名包）:
 *    文本头:
 *      nopUpgrade
 *      version=x.y.z
 *      md5=<payload 的 32 位 hex>
 *      force=0|1          # 可选；1=强制升级
 *      <空行>
 *      <squashfs payload>
 *    或以 "nopUpgrade" 起头但无 version 字段：视为强制升级（工厂包）。
 *
 *  规则:
 *    1) 必须校验 payload MD5，不符则失败。
 *    2) 包版本必须高于当前 system.fw_version，除非强制升级。
 *    3) 强制升级：头 force=1，或命令 force/forceUpgrade，或以 nopUpgrade 起头且无 version。
 *    4) 通过后交 /dvr/bin/nvr_do_update.sh 写非活动 rootfs（A/B 备份当前槽）并切槽重启。
 ***************************************************************************************/
#ifndef NVR_OTA_H
#define NVR_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_OTA_MAGIC "nopUpgrade"

/* src: http(s) URL 或本地路径(FileName)。md5_hex: 32 位 hex（头里有则头优先）。
 * cur_version: 当前固件版本。pkg_version: 无包头时用命令里的 version。
 * force: 跳过「版本必须更高」。返回 0=已启动，-2=忙，<0=参数错。 */
int  nvr_ota_start(const char *src, const char *staging, const char *updater,
                   const char *md5_hex, const char *cur_version,
                   const char *pkg_version, int force);

int  nvr_ota_progress(void);
const char *nvr_ota_state(void);
void nvr_ota_deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_OTA_H */
