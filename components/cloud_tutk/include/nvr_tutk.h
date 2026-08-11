/***************************************************************************************
 *  nvr_tutk.h — ④ TUTK P2P glue（NVR 设备端 · P2PTunnel 端口映射）
 *
 *  App 经 P2PTunnel 映射:
 *    · NOP 命令 → localhost:nop_port (8089)
 *    · Live RTSP → localhost:rtsp_port (554) → nvr_rtsp_live
 *
 *  IOTC auth key 经 get/setIotcAuthKey 写入设置库 tutk.authkey。
 ***************************************************************************************/
#ifndef NVR_TUTK_H
#define NVR_TUTK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_TUTK_AUTH_KEY_LEN 8

typedef struct {
    const char *uid;
    const char *auth_key;       /* 8 字符 IOTC key;空串=无 key 登录 */
    const char *license_key;    /* TUTK_SDK_Set_License_Key;可 NULL */
    int         nop_port;       /* 默认 8089 */
    int         rtsp_port;      /* 默认 554 */
    int         max_sessions;   /* 默认 8 */
} nvr_tutk_cfg_t;

int  nvr_tutk_init(const nvr_tutk_cfg_t *cfg);
int  nvr_tutk_start(void);
void nvr_tutk_stop(void);
void nvr_tutk_deinit(void);

/* 运行中更新 auth key(优先 IOTC_Device_Update_Authkey;失败则需 restart) */
int  nvr_tutk_update_authkey(const char *auth_key);

int  nvr_tutk_running(void);
int  nvr_tutk_online(void);    /* 当前 P2PTunnel/IOTC 会话数 */

#ifdef __cplusplus
}
#endif
#endif /* NVR_TUTK_H */
