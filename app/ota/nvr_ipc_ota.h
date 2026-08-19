/***************************************************************************************
 *  nvr_ipc_ota.h — 通道相机固件：NVR 下载后下推（NOP upload.cgi / ONVIF StartFirmwareUpgrade）
 ***************************************************************************************/
#ifndef NVR_IPC_OTA_H
#define NVR_IPC_OTA_H

#include "nvr_channel.h"
#include "nop_sdk/nop_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nvr_ipc_ota_job {
    int               chn;          /* 0-based */
    nvr_channel_t     ch;
    nvr_chan_mgr_t   *cm;
    nop_app_t        *nop;
    char              query_url[512]; /* 空=不查服务器（本地 USB 文件） */
    char              local_file[256]; /* 非空=已有文件，跳过下载 */
    char              cur_ver[64];
    int               automatic;    /* 0=只下载 status=3；1=下载后推相机 */
} nvr_ipc_ota_job_t;

/* 0=已启动；-2=忙；<0=参数错。立即返回，后台下载/下推。 */
int  nvr_ipc_ota_start(const nvr_ipc_ota_job_t *job);
/* status 0..4（与 X_NightOwl_checkChannelUpgradeStatus 一致）。 */
int  nvr_ipc_ota_status(int chn, int *error_out);
void nvr_ipc_ota_set_status(int chn, int status, int error);

#ifdef __cplusplus
}
#endif
#endif /* NVR_IPC_OTA_H */
