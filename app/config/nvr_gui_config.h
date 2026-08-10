/***************************************************************************************
 *  nvr_gui_config.h — 读写 LVGL 共享的 GUI_CONFIG.json。
 *
 *  文件(设备上 /mnt/custom/GUI_CONFIG.json)由 LVGL/NVR 共享:
 *    { "channels":[PoE数,LAN数], "allDisplayModes":[...], "displayMode":9, "displayPage":1 }
 *  - displayMode/displayPage:出图宫格与页;setDeviceDisplayMode 写、get 读、开机读它出图。
 *  - channels=[PoE,LAN]:设备可分配通道容量;开机据此定容量,超量不添加。
 *
 *  路径优先级(nvr_gui_config_init 解析并记住):
 *    $NVR_GUI_CONFIG → /mnt/custom/GUI_CONFIG.json → <config_dir>/GUI_CONFIG.json
 ***************************************************************************************/
#ifndef NVR_GUI_CONFIG_H
#define NVR_GUI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 解析并记住 GUI_CONFIG.json 路径。启动调用一次(未调用则默认 /mnt/custom/GUI_CONFIG.json)。 */
void nvr_gui_config_init(const char *config_dir);

/* 读出图宫格/页(读不到给默认 9 / 1)。返回 0。 */
int  nvr_gui_config_get_display(int *mode, int *page);

/* 写出图宫格/页(保留文件其它字段,原子写)。
 * mode==0(退出 Liveview 的瞬时态)不写文件、直接返回 0。 */
int  nvr_gui_config_set_display(int mode, int page);

/* 读通道容量 channels=[PoE, LAN](读不到给默认 16 / 16)。返回 0。 */
int  nvr_gui_config_get_channels(int *poe_n, int *lan_n);

#ifdef __cplusplus
}
#endif
#endif /* NVR_GUI_CONFIG_H */
