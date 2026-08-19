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

/* 读直播宫格清单 allDisplayModes(无则默认 1,4,9,16)。out 写模式值,返回个数(≤cap)。 */
int  nvr_gui_config_get_live_modes(int *out, int cap);

/* 读回放宫格清单 allPlaybackDisplayModes(无则回落 allDisplayModes;再无则默认 1,4,9,16)。
 * out 写模式值,返回个数(≤cap)。 */
int  nvr_gui_config_get_playback_modes(int *out, int cap);

/* 最大同屏回放路数:取 allPlaybackDisplayModes 最大值,并 clamp 到 PoE+LAN 容量。 */
int  nvr_gui_config_max_playback_channels(void);

#ifdef __cplusplus
}
#endif
#endif /* NVR_GUI_CONFIG_H */
