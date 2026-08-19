/***************************************************************************************
 *  nvr_gui_notify.h — App 向导状态 → GUI_longPolling 切页 / BLE 解锁。
 *
 *  notify_appSetupStatus 写入此处;GUI_longPolling 带 APPNotifySetupStatus;
 *  nvr_app 据此刷新 BLE 广播 locked/unlocked。
 ***************************************************************************************/
#ifndef NVR_GUI_NOTIFY_H
#define NVR_GUI_NOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/* App 协议 status: 0 BLE 配对页 / 1 P2P 配对页 / 2 向导结束进 liveview / 3 关登录窗 */
void nvr_gui_set_setup_status(int status);
/* 有过 notify 则 *has=1 并返回当前 status;从未 notify 则 *has=0。 */
int  nvr_gui_get_setup_status(int *has);
int  nvr_gui_setup_gen(void);

void nvr_gui_set_ui_unlocked(int on);
int  nvr_gui_ui_unlocked(void);

#ifdef __cplusplus
}
#endif
#endif
