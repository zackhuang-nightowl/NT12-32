/***************************************************************************************
 *  nvr_defaults.h — 编译期可配置默认值(统一放这里,编译时改这里即可)。
 *
 *  说明:这些是"缺省/初始值",运行期若有配置源(GUI_CONFIG.json / 设置库 / dts / 烧录分区)
 *        会覆盖。产测/不同机型改本文件即可,不必改业务代码。
 *  出站 HTTP(S) URL 单独放 [nvr_urls.h](nvr_urls.h)（ServeDomainV2；`-DNVR_STAGE=ON` 为 stage）。
 *
 *  ⚠️ 出厂烧录数据不在此:sn / mac / NID / keyx / keyy 由产测烧录到独立分区,运行期读取。
 *     见 docs/烧录分区数据.md(待补)。SN 不设编译默认。
 ***************************************************************************************/
#ifndef NVR_DEFAULTS_H
#define NVR_DEFAULTS_H

/* ================= A · 设备标识 / 容量 ================= */
#define NVR_DEF_MODEL       "NOP12-32"  /* 机型(权威源 /User/OWLModel;此为回退缺省) */
#define NVR_DEF_NAME        "NVR"       /* 设备名缺省 */
#define NVR_DEF_FW_VERSION  "1.0.0"           /* 固件版本(getDeviceInfo/TUTK profile) */
#define NVR_DEF_TUTK_DEV_TYPE "videoRecorder" /* TUTK profile 设备类型(APP 向导契约) */
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
/* eth1 PoE VLAN 基号:物理口 P → vid = base + P。
 * ★ base=2001 → 口 P 走 VLAN(2001+P)=2002..2017,与 PoE 交换芯片对物理口的 tag 一致
 *   (实测 口1→VLAN2002、口3→VLAN2004)。段(198.18.<seg>)仍取口号 P,故口↔段 1:1
 *   (口3→段3),且口16→VLAN2017 有对应接口。VLAN2001 留作 NVR 管理口。 */
#define NVR_DEF_VLAN_BASE   2001   /* 本机 PoE 交换芯片 tag 口P→VLAN(2001+P);eth1.(2001+P)=段P。实测口9→VLAN2010 */
#define NVR_DEF_TIMEZONE    "UTC"       /* 缺省时区(系统时钟恒 UTC,时区仅显示) */
/* NTP 保留 3 个:主(当前)+ Apple + Google */
#define NVR_DEF_NTP1        "pool.ntp.org"
#define NVR_DEF_NTP2        "time.apple.com"
#define NVR_DEF_NTP3        "time.google.com"

/* ================= B2 · TUTK P2P(ODC agent;凭据权威源 /User) ===== */
#define NVR_TUTK_AUTH_KEY_LEN   8
#define NVR_DEF_TUTK_AUTHKEY    "00000000"  /* 出厂 IotcAuthKey,见 APP_client_Agent.md */
#define NVR_DEF_TUTK_LICENSE    "000000"

/* ================= C · 路径 ================= */
#define NVR_DEF_OTA_STAGING "/mnt/update.rom"              /* OTA 固件下载暂存(下面烧写) */
#define NVR_DEF_OTA_UPDATER "/dvr/bin/nvr_do_update.sh"    /* 下载校验后交更新器烧 A/B */
#define NVR_DEF_GUI_CONFIG  "/mnt/custom/GUI_CONFIG.json"  /* LVGL 共享出图/容量配置 */
#define NVR_DEF_IPC_OTA_STAGING "/tmp/ipc_ota.bin"         /* 通道固件下载暂存 */

/* ================= 出站请求 URL（OTA / Cognito / GraphQL / VSaaS）========
 * 域名与路径集中在 nvr_urls.h；改云环境只改那一张表。 */
#include "nvr_urls.h"
#define NVR_DEF_OTA_URL_BASE     NVR_URL_OTA_BASE
#define NVR_DEF_OTA_ENV          NVR_URL_OTA_ENV
#define NVR_DEF_OTA_NVR_PRODUCT  NVR_URL_OTA_NVR_PRODUCT
#define NVR_DEF_OTA_CAM_PRODUCT  NVR_URL_OTA_CAM_PRODUCT

/* ================= D · 显示 / 出图 ================= */
#define NVR_DEF_HDMI_W      1920        /* HDMI 缺省宽(按屏可切 4K,失败回落) */
#define NVR_DEF_HDMI_H      1080        /* HDMI 缺省高 */
#define NVR_DEF_LAYOUT      16          /* 缺省分屏布局 */
#define NVR_DEF_GUI_MODE    9           /* GUI 缺省宫格(GUI_CONFIG 读不到时) */
#define NVR_DEF_GUI_PAGE    1           /* GUI 缺省页 */
#define NVR_DEF_ZOOM_MIN    100         /* 数字变焦下限：接口 ZoomRatio 100=1.00x 原画 */
#define NVR_DEF_ZOOM_MAX    1000        /* 数字变焦上限：接口 ZoomRatio 0~1000 */
#define NVR_DEF_WAIT_READY_MS 2000      /* 切宫格/切码流后等图上限 */
#define NVR_DEF_WAIT_EXT_MS   800       /* Ext 换 channel(有缓存 IDR)等图上限 */
#define NVR_DEF_SETTLE_MS   150         /* 首格出图后再沉降,让帧真到面板 */
#define NVR_DEF_CMD_TIMEOUT_S  8        /* 阻塞 GUI 的 set/动作：完成后或超时再回 */
#define NVR_DEF_CMD_CONNECT_S  3        /* 透传/相机 HTTP 连接超时 */

#endif /* NVR_DEFAULTS_H */
