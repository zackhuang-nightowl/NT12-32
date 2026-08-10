/***************************************************************************************
 *  nvr_display_modes.h — HDMI 输出分辨率「单一来源」(single source of truth)。
 *
 *  ★ 新增/调整一档输出分辨率 = 只改 NVR_DISPLAY_MODES 加/改一行,别处派生:
 *      - mhal_vout_init 的降级阶梯(从高到低逐级下探到屏幕可用者)
 *      - GUI_getSysDisplay 的 resolutionList / setSysDisplay 的合法性校验
 *
 *  一行三列:X(width, height, "WxH")。顺序 = 降级优先级(高→低),第一档为最高。
 ***************************************************************************************/
#ifndef NVR_DISPLAY_MODES_H
#define NVR_DISPLAY_MODES_H

/*      width  height  label        */
#define NVR_DISPLAY_MODES(X) \
    X(3840, 2160, "3840x2160") \
    X(1920, 1080, "1920x1080") \
    X(1280,  720, "1280x720")

/* 默认/兜底分辨率(阶梯保底档,任何屏幕都应支持)。 */
#define NVR_DISPLAY_DEFAULT_W 1920
#define NVR_DISPLAY_DEFAULT_H 1080

#endif /* NVR_DISPLAY_MODES_H */
