/***************************************************************************************
 *  nvr_cmd_internal.h — 路由层内部契约:统一 ctx、handler 签名、全部 cmd_<func> 声明。
 *
 *  约定(镜像 components/nop/src/onvif/mapping/CONVENTIONS.md):
 *    - 每个本地处理的 NOP 接口 = 一个命名 handler cmd_<func>,与 func 名一一对应,便于审计。
 *    - handler 统一签名 nvr_cmd_fn:入 args(cJSON,可空)+ ctx,返回 malloc 的应答串(调用方 free)。
 *    - 中央路由表 g_nvr_cmd_table[]（nvr_cmd_table.c）是唯一权威:新增本地接口 = 加一行 + 一个 handler。
 ***************************************************************************************/
#ifndef NVR_CMD_INTERNAL_H
#define NVR_CMD_INTERNAL_H

#include "cJSON.h"
#include "nvr_settings.h"
#include "nvr_channel.h"
#include "nvr_storage.h"
#include "rsdk.h"
#include "nop_sdk/nop_app.h"
#include "nvr_preview.h"
#include "nvr_chan_persist.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 所有本地 handler 共享的上下文(由 router 装配填充)。
 * 带 tag(nvr_cmd_ctx_t)以便 nvr_cmd_util.h 用 `struct nvr_cmd_ctx_t` 前向声明,
 * 否则匿名 struct 与 struct-tag 前向声明为不同类型→nvr_cmd_nop_dispatch 类型冲突。 */
typedef struct nvr_cmd_ctx_t {
    nvr_settings_t     *settings;     /* KV + 结构化表 */
    nvr_chan_mgr_t     *cm;           /* channel→设备 解析 / 状态 / LAN 增删 */
    nvr_storage_t      *stg;          /* 存储信息/格式化/健康 */
    struct nvr_stream_mgr *sm;        /* 拉流/录像管理器:格式化后重组装盘组→补开 writer 用 */
    rsdk_group_t       *group;        /* 事件/录像 查询 */
    void               *meta;         /* rsdk_meta ctx(事件列表/日历);可 NULL */
    nop_app_t          *nop;          /* 回落 nopcore(dispatch 用) */
    nvr_preview_t      *pv;           /* 出图:显示指令驱动 */
    struct nvr_playback *pb;          /* 本机回放引擎(GUI_playbackControl) */
    struct nvr_evt_hub *eh;           /* 事件中枢:longPolling 的 Motion/Human/Face/Car 位图 */
    nvr_chan_persist_t *persist;      /* 出图↔channel 映射(channels.json) */
    struct nvr_talk    *talk;         /* 双向对讲 */
    void               *disp_user;    /* 分辨率热切回调上下文(app) */
    void              (*on_set_resolution)(void *disp_user, int w, int h);  /* setSysDisplay 热切 */
    int                 dev_nop_port; /* 透传到设备的 NOP 端口,默认 8089 */
    char                nvr_sn[64];   /* 本机 SN(nopOnvif 激活密码用) */
} nvr_cmd_ctx_t;

/* handler:入 args + ctx → 返回 malloc 的完整应答 JSON(调用方 free)。 */
typedef char *(*nvr_cmd_fn)(cJSON *args, const nvr_cmd_ctx_t *ctx);

/* 路由表行。 */
typedef struct { const char *func; nvr_cmd_fn fn; } nvr_cmd_route_t;

/* 中央表 + 查表(nvr_cmd_table.c)。 */
extern const nvr_cmd_route_t g_nvr_cmd_table[];
extern const int             g_nvr_cmd_table_len;
nvr_cmd_fn nvr_cmd_table_lookup(const char *func);

/* ============================ 各域 handler 声明 ============================ */
/* --- display 出图(nvr_cmd_display.c) --- */
char *cmd_GUI_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getDeviceDisplayMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setChannelMapping(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getChannelMapping(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setDeviceDisplayExt(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getDeviceDisplayExt(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setSysDisplay(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_longPolling(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_notify_appSetupStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_playbackControl(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getPlaybackCapabilities(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getPlaybackMode(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getDeviceCapabilities(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_AI_getChannelAICapabilities(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelZoomPan(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelZoomPan(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCableConnectStatus(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- lan 子设备(nvr_cmd_lan.c) --- */
char *cmd_GUI_LanSearch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_GetAddedLanDevices(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getLanDevice(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setLanDevice(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_LanAddDevice(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_LanDelDevice(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_attachIPDevices(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_detachIPDevice(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getAttachStatus(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- system 设备/账户(nvr_cmd_system.c) --- */
char *cmd_setName(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getName(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getInputChannelNames(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getInputChannelName(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setInputChannelName(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getDeviceInfo(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setTimezone(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getTimezone(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_set_datetime(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_get_datetime(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setTimeSyncSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getTimeSyncSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_reboot(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_resetToFactorySettings(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setOwner(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getOwner(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_updateP2PCredential(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_loginUser(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_unlock(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getRemoteAccessState(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setRemoteAccessState(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getFeatureList(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getIotcAuthKey(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getIotcUID(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getAvPassword(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getAvAccount(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getProfile(cJSON *a, const nvr_cmd_ctx_t *c);
/* P2P APP 远程媒体(nvr_cmd_p2p.c) */
char *cmd_getLiveCapabilities(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_startLiveStream(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_stopLiveStream(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_startPlayback(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_stopPlayback(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getSpeakerCapabilities(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_startSpeaker(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_stopSpeaker(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_buildTunnel(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setIotcAuthKey(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setAvPassword(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setIotcUID(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_notifyLoginSuccess(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_notifySessionCount(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getNotificationSetting(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getUID(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getAutoRebootSetting(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setAutoRebootSetting(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getSystemLog(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- account 鉴权(nvr_cmd_account.c) --- */
char *cmd_GUI_login(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_logout(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_LoginPage(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getLoginStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_createUser(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_deleteUser(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setUser(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getUsers(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getUserGroupPermissions(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_forgetPassword(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- misc 通道聚合(nvr_cmd_misc.c) --- */
char *cmd_getChannelsStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelStats(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelLoading(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getEnhancedSecurity(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setEnhancedSecurity(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getDeviceActive(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setDeviceActive(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCurrentClouds(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelInfo(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCloudStatusHistory(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelCloudRecordStats(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelCloudRecordStatsSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setChannelCloudRecordStatsSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelRecordingContent(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getReportServer(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getEnvironment(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getLog(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- network 网络(nvr_cmd_network.c) --- */
char *cmd_GUI_getLocalLink(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setLocalLink(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getLanInterface(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getWanInterface(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getWanInterface(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getNetPort(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setNetPort(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getNTP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setNTP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getDDNS(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setDDNS(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getUPnP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setUPnP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getFTP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setFTP(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_testEmailAlert(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getPoE(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setPoE(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- cloud 云存(nvr_cmd_cloud.c) --- */
char *cmd_X_NightOwl_setCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCloudRecordSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setCloudRecordConfigs(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCloudRecordConfigs(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCloudRecordLogConfig(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setCloudRecordLogConfig(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_startCloudRecordTest(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_stopCloudRecordTest(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCloudRecordTestProgress(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- record 录像/推送(nvr_cmd_record.c) --- */
char *cmd_X_NightOwl_setChannelRecordingTriggers(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelRecordingTriggers(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelContinuousScheduleRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelContinuousScheduleRecordingSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelContinuousRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelContinuousRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- storage 存储(nvr_cmd_storage.c) --- */
char *cmd_getStorageInfo(cJSON *a, const nvr_cmd_ctx_t *c);   /* getStorageInfo 与 X_NightOwl_getStorageInfo 共用 */
char *cmd_formatStorage(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getAllDisksHealth(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getCurrentStorage(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setCurrentStorage(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getHddConfig(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setHddConfig(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- playback 回放(nvr_cmd_playback.c) --- */
char *cmd_GUI_getPlaybackAudio(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setPlaybackAudio(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getFileList(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_ChannelBackupFiles(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_GetChannelBackupStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_startRecordingBackup(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getRecordingBackupProgress(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_StopChannelBackup(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_getChannelEventRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_setChannelEventRecordingSchedule(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getChannelRecordingTime(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setChannelRecordingTime(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- ota 升级(nvr_cmd_ota.c) --- */
char *cmd_upgradeFirmware(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_checkFirmwareUpgradeStatus(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_checkServerFirmware(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_checkChannelServerFirmware(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_GUI_upgradeChannelFirmware(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_upgradeChannelFirmware(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_checkChannelUpgradeStatus(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- event 事件(nvr_cmd_event.c) --- */
char *cmd_X_NightOwl_queryEventList(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_queryEventListWithSpecificOrder(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_queryEventCalendar(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_queryContinuousCalendar(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_queryRecordingInterval(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_AI_getEventExtInfo(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_AI_getEventExtInfoBatchByReverseTime(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_AI_getEventExtInfoConfig(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_AI_setEventExtInfoConfig(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_snapshotChannel(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getEventDownloadCapability(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_startEventDownload(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getEventDownloadProgress(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_startEventDownloadwithURL(cJSON *a, const nvr_cmd_ctx_t *c);

/* --- push 推送(nvr_cmd_push.c) --- */
char *cmd_X_NightOwl_getChannelPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelsPushNotificationSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelPushNotificationTriggers(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelPushNotificationTriggers(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_getChannelPushNotificationDoNotDisturb(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_X_NightOwl_setChannelPushNotificationDoNotDisturb(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setSnooze(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getSnooze(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_setPushPhotoSwitch(cJSON *a, const nvr_cmd_ctx_t *c);
char *cmd_getPushPhotoSwitch(cJSON *a, const nvr_cmd_ctx_t *c);

#ifdef __cplusplus
}
#endif
#endif /* NVR_CMD_INTERNAL_H */
