/***************************************************************************************
 *  nvr_tutk.h — ④ TUTK P2P glue（NVR 设备端）
 *
 *  NVR 作为 TUTK "设备"：登录 IOTC → 监听 App 连接 → 每会话开 AV server →
 *  把 streaming 的码流经 avSendFrameData 推给远程 App。
 *
 *  底层：third_party/tutk_sdk（IOTCAPIs / AVAPIs / IOTCDevice / AVServer）。
 ***************************************************************************************/
#ifndef NVR_TUTK_H
#define NVR_TUTK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1) 初始化 + 设备登录（uid/auth_key 出厂写入或注册获取；见 config/cloud_tutk.json） */
int  nvr_tutk_init(const char *uid, const char *auth_key);

/* 2) 起监听线程（等待 App 连接，接受即开 AV server） */
int  nvr_tutk_start(void);
void nvr_tutk_stop(void);
void nvr_tutk_deinit(void);

/* 3) 推一帧视频给所有在线会话（由 ③ streaming 的旁路调用；codec: 0=H264 1=H265） */
int  nvr_tutk_send_video(int chn, const uint8_t *data, int len, int codec,
                         int is_key, uint32_t ts_ms);

/* 在线客户端数（诊断/限流用） */
int  nvr_tutk_online(void);

#ifdef __cplusplus
}
#endif
#endif
