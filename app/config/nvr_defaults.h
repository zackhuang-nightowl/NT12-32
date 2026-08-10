/***************************************************************************************
 *  nvr_defaults.h — 编译期可配置默认值(统一放这里,编译时改这里即可)。
 *
 *  说明:这些是"缺省/初始值",运行期若有配置源(GUI_CONFIG.json / 设置库 / dts / 烧录分区)
 *        会覆盖。产测/不同机型改本文件即可,不必改业务代码。
 *
 *  ⚠️ 出厂烧录数据不在此:sn / mac / NID / keyx / keyy 由产测烧录到独立分区,运行期读取。
 *     见 docs/烧录分区数据.md(待补)。SN 不设编译默认。
 ***************************************************************************************/
#ifndef NVR_DEFAULTS_H
#define NVR_DEFAULTS_H

/* ================= A · 设备标识 / 容量 ================= */
#define NVR_DEF_MODEL       "NT12-32"   /* 机型(setDeviceInfo/配置缺省) */
#define NVR_DEF_NAME        "NVR"       /* 设备名缺省 */
#define NVR_DEF_CAPACITY    32          /* 整机通道容量(= 16 PoE + 16 LAN) */

/* ================= 通道容量缺省(GUI_CONFIG.json channels=[PoE,LAN] 读不到时用) ===== */
#define NVR_DEF_GUI_POE_N   16          /* 缺省 PoE 通道数 */
#define NVR_DEF_GUI_LAN_N   16          /* 缺省 LAN 通道数 */

/* ================= PoE 内网段(NVR 自建 udhcpd 给相机的 198.18.<口>.x) ============= */
#define NVR_POE_NET_A       198         /* 第 1 字节 */
#define NVR_POE_NET_B       18          /* 第 2 字节 */
#define NVR__STR2(x)        #x
#define NVR__STR(x)         NVR__STR2(x)
#define NVR_POE_NET_PREFIX  NVR__STR(NVR_POE_NET_A) "." NVR__STR(NVR_POE_NET_B)   /* "198.18" */
#define NVR_POE_CAM_IP_PAT  NVR_POE_NET_PREFIX ".%d.1"     /* 原厂相机 = .1 */
#define NVR_POE_NVR_IP_PAT  NVR_POE_NET_PREFIX ".%d.100"   /* NVR 侧 = .100 */

/* ================= B · 网络 / 端口 ================= */
#define NVR_DEF_NOP_PORT    8089        /* NOP/HTTP 入口端口(唯一 8089 入口) */
#define NVR_DEF_ONVIF_PORT  80          /* 相机 ONVIF 默认端口 */
#define NVR_DEF_ETH0_IP     "192.168.1.100"    /* eth0 静态缺省 IP */
#define NVR_DEF_ETH0_MASK   "255.255.255.0"    /* eth0 静态缺省掩码 */
#define NVR_DEF_VLAN_BASE   2000        /* eth1 PoE VLAN 基号(口 P → vid = base + P) */
#define NVR_DEF_TIMEZONE    "UTC"       /* 缺省时区(系统时钟恒 UTC,时区仅显示) */
/* NTP 保留 3 个:主(当前)+ Apple + Google */
#define NVR_DEF_NTP1        "pool.ntp.org"
#define NVR_DEF_NTP2        "time.apple.com"
#define NVR_DEF_NTP3        "time.google.com"

/* ================= C · 路径 ================= */
#define NVR_DEF_OTA_STAGING "/mnt/update.rom"              /* OTA 固件下载暂存(下面烧写) */
#define NVR_DEF_GUI_CONFIG  "/mnt/custom/GUI_CONFIG.json"  /* LVGL 共享出图/容量配置 */

/* ================= D · 显示 / 出图 ================= */
#define NVR_DEF_HDMI_W      1920        /* HDMI 缺省宽(按屏可切 4K,失败回落) */
#define NVR_DEF_HDMI_H      1080        /* HDMI 缺省高 */
#define NVR_DEF_LAYOUT      16          /* 缺省分屏布局 */
#define NVR_DEF_GUI_MODE    9           /* GUI 缺省宫格(GUI_CONFIG 读不到时) */
#define NVR_DEF_GUI_PAGE    1           /* GUI 缺省页 */
#define NVR_DEF_ZOOM_MIN    100         /* 数字变焦下限 1.00x */
#define NVR_DEF_ZOOM_MAX    500         /* 数字变焦上限 5.00x */
#define NVR_DEF_WAIT_READY_MS 2000      /* 切宫格/切码流后等图上限 */
#define NVR_DEF_SETTLE_MS   150         /* 首格出图后再沉降,让帧真到面板 */

#endif /* NVR_DEFAULTS_H */
