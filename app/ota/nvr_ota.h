/***************************************************************************************
 *  nvr_ota.h — NVR 固件自升级（下载 .rom → 校验 → 交设备更新器 → 重启）
 *
 *  upgradeFirmware{url} → 后台线程下载到 staging，进度可查；完成后调设备更新钩子。
 *  实际烧写由设备更新脚本/机制完成（本模块负责下载+暂存+触发）。
 ***************************************************************************************/
#ifndef NVR_OTA_H
#define NVR_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/* 启动一次固件下载升级。url=.rom 地址；staging 为落地路径(如 /mnt/update.rom)；
 * updater 为下载完成后要执行的更新命令(如 "/dvr/bin/nvr_do_update.sh")，可 NULL(仅暂存)。
 * 返回 0=已启动(后台进行)，<0=忙/参数错。 */
int  nvr_ota_start(const char *url, const char *staging, const char *updater);

/* 进度：0..100；-1=失败；100=完成(即将触发更新)。 */
int  nvr_ota_progress(void);
/* 状态串："idle"/"downloading"/"verifying"/"updating"/"failed"/"done"。 */
const char *nvr_ota_state(void);

void nvr_ota_deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_OTA_H */
