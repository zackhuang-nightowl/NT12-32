/***************************************************************************************
 *  nvr_cmd_table.c — 唯一权威路由表:NOP func 名 → 命名 handler(黑名单 = 本表)。
 *
 *  新增本地接口 = 在此加一行 + 在对应域文件写一个 cmd_<func>。手动加行,不自动注册。
 *  无 NVR 真实实现、标记暂不实现的行用行尾注释「暂不实现:501」标注。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include <string.h>

const nvr_cmd_route_t g_nvr_cmd_table[] = {
    /* --- display 出图 --- */
    { "GUI_setDeviceDisplayMode",              cmd_GUI_setDeviceDisplayMode },
    { "GUI_getDeviceDisplayMode",              cmd_GUI_getDeviceDisplayMode },
    { "setDeviceDisplayMode",                  cmd_setDeviceDisplayMode },
    { "getDeviceDisplayMode",                  cmd_getDeviceDisplayMode },
    { "GUI_setChannelMapping",                 cmd_GUI_setChannelMapping },
    { "GUI_getChannelMapping",                 cmd_GUI_getChannelMapping },
    { "GUI_setDeviceDisplayExt",               cmd_GUI_setDeviceDisplayExt },
    { "GUI_getDeviceDisplayExt",               cmd_GUI_getDeviceDisplayExt },
    { "GUI_getSysDisplay",                     cmd_GUI_getSysDisplay },
    { "GUI_setSysDisplay",                     cmd_GUI_setSysDisplay },
    { "X_NightOwl_getChannelStatus",           cmd_X_NightOwl_getChannelStatus },
    { "GUI_longPolling",                       cmd_GUI_longPolling },
    { "notify_appSetupStatus",                 cmd_notify_appSetupStatus },
    { "notify_APPSetupStatus",                 cmd_notify_appSetupStatus },
    { "GUI_playbackControl",                   cmd_GUI_playbackControl },
    { "GUI_setPlaybackMode",                   cmd_GUI_setPlaybackMode },
    { "GUI_getPlaybackMode",                   cmd_GUI_getPlaybackMode },
    { "getPlaybackCapabilities",               cmd_getPlaybackCapabilities },
    { "getLiveCapabilities",                   cmd_getLiveCapabilities },
    { "startLiveStream",                       cmd_startLiveStream },
    { "stopLiveStream",                        cmd_stopLiveStream },
    { "startPlayback",                         cmd_startPlayback },
    { "stopPlayback",                          cmd_stopPlayback },
    { "getSpeakerCapabilities",                cmd_getSpeakerCapabilities },
    { "startSpeaker",                          cmd_startSpeaker },
    { "stopSpeaker",                           cmd_stopSpeaker },
    { "buildTunnel",                           cmd_buildTunnel },
    { "X_NightOwl_getDeviceCapabilities",      cmd_X_NightOwl_getDeviceCapabilities },
    { "AI_getChannelAICapabilities",           cmd_AI_getChannelAICapabilities },
    { "X_NightOwl_setChannelZoomPan",          cmd_X_NightOwl_setChannelZoomPan },
    { "X_NightOwl_getChannelZoomPan",          cmd_X_NightOwl_getChannelZoomPan },
    { "getCableConnectStatus",                 cmd_getCableConnectStatus },

    /* --- lan 子设备 --- */
    { "GUI_LanSearch",                         cmd_GUI_LanSearch },
    { "GUI_GetAddedLanDevices",                cmd_GUI_GetAddedLanDevices },
    { "GUI_getLanDevice",                      cmd_GUI_getLanDevice },
    { "GUI_setLanDevice",                      cmd_GUI_setLanDevice },
    { "GUI_LanAddDevice",                      cmd_GUI_LanAddDevice },
    { "GUI_LanDelDevice",                      cmd_GUI_LanDelDevice },
    { "X_NightOwl_attachIPDevices",            cmd_X_NightOwl_attachIPDevices },
    { "X_NightOwl_detachIPDevice",             cmd_X_NightOwl_detachIPDevice },
    { "X_NightOwl_getAttachStatus",            cmd_X_NightOwl_getAttachStatus },

    /* --- system 设备 --- */
    { "setName",                               cmd_setName },
    { "getName",                               cmd_getName },
    { "X_NightOwl_getInputChannelNames",       cmd_X_NightOwl_getInputChannelNames },
    { "X_NightOwl_getInputChannelName",        cmd_X_NightOwl_getInputChannelName },
    { "X_NightOwl_setInputChannelName",        cmd_X_NightOwl_setInputChannelName },
    { "getDeviceInfo",                         cmd_getDeviceInfo },
    { "X_NightOwl_setTimezone",                cmd_X_NightOwl_setTimezone },
    { "X_NightOwl_getTimezone",                cmd_X_NightOwl_getTimezone },
    { "set_datetime",                          cmd_set_datetime },
    { "get_datetime",                          cmd_get_datetime },
    { "X_NightOwl_setTimeSyncSwitch",          cmd_X_NightOwl_setTimeSyncSwitch },
    { "X_NightOwl_getTimeSyncSwitch",          cmd_X_NightOwl_getTimeSyncSwitch },
    { "reboot",                                cmd_reboot },
    { "X_NightOwl_resetToFactorySettings",     cmd_X_NightOwl_resetToFactorySettings },
    { "X_NightOwl_setOwner",                   cmd_X_NightOwl_setOwner },
    { "X_NightOwl_getOwner",                   cmd_X_NightOwl_getOwner },
    { "X_NightOwl_updateP2PCredential",        cmd_X_NightOwl_updateP2PCredential },
    { "X_NightOwl_loginUser",                  cmd_X_NightOwl_loginUser },
    { "X_NightOwl_unlock",                     cmd_X_NightOwl_unlock },
    { "GUI_getRemoteAccessState",              cmd_GUI_getRemoteAccessState },
    { "GUI_setRemoteAccessState",              cmd_GUI_setRemoteAccessState },
    { "GUI_getFeatureList",                    cmd_GUI_getFeatureList },
    { "getIotcAuthKey",                        cmd_getIotcAuthKey },
    { "setIotcAuthKey",                        cmd_setIotcAuthKey },
    { "getIotcUID",                            cmd_getIotcUID },
    { "setIotcUID",                            cmd_setIotcUID },
    { "getAvPassword",                         cmd_getAvPassword },
    { "setAvPassword",                         cmd_setAvPassword },
    { "getAvAccount",                          cmd_getAvAccount },
    { "getProfile",                            cmd_getProfile },
    { "notifyLoginSuccess",                    cmd_notifyLoginSuccess },
    { "notifySessionCount",                    cmd_notifySessionCount },
    { "getNotificationSetting",                cmd_getNotificationSetting },
    { "GUI_getUID",                            cmd_GUI_getUID },
    { "GUI_getAutoRebootSetting",              cmd_GUI_getAutoRebootSetting },
    { "GUI_setAutoRebootSetting",              cmd_GUI_setAutoRebootSetting },
    { "GUI_getSystemLog",                      cmd_GUI_getSystemLog },           /* 暂不实现:501 */

    /* --- account 鉴权 --- */
    { "GUI_login",                             cmd_GUI_login },
    { "GUI_logout",                            cmd_GUI_logout },
    { "GUI_LoginPage",                         cmd_GUI_LoginPage },              /* GUI 进出登录窗通知 */
    { "GUI_getLoginStatus",                    cmd_GUI_getLoginStatus },
    { "GUI_createUser",                        cmd_GUI_createUser },
    { "GUI_deleteUser",                        cmd_GUI_deleteUser },
    { "GUI_setUser",                           cmd_GUI_setUser },
    { "GUI_getUsers",                          cmd_GUI_getUsers },
    { "GUI_getUserGroupPermissions",           cmd_GUI_getUserGroupPermissions },
    { "GUI_forgetPassword",                    cmd_GUI_forgetPassword },

    /* --- misc 通道聚合/安全 --- */
    { "getChannelsStatus",                     cmd_getChannelsStatus },
    { "X_NightOwl_getChannelInfo",             cmd_X_NightOwl_getChannelInfo },
    { "getChannelStats",                       cmd_getChannelStats },            /* 暂不实现:501 */
    { "getChannelLoading",                     cmd_getChannelLoading },          /* 暂不实现:501 */
    { "getEnhancedSecurity",                   cmd_getEnhancedSecurity },        /* NVR 代查 NOP digest random */
    { "setEnhancedSecurity",                   cmd_setEnhancedSecurity },        /* NVR 代开/关 digest，入库 P_enh/空 */
    { "X_NightOwl_getDeviceActive",            cmd_X_NightOwl_getDeviceActive }, /* NVR 代查/代激活 nopOnvif */
    { "X_NightOwl_setDeviceActive",            cmd_X_NightOwl_setDeviceActive },
    { "getCurrentClouds",                      cmd_getCurrentClouds },
    { "getCloudStatusHistory",                 cmd_getCloudStatusHistory },      /* 暂不实现:501 */
    { "getChannelCloudRecordStats",            cmd_getChannelCloudRecordStats }, /* 暂不实现:501 */
    { "getChannelCloudRecordStatsSwitch",      cmd_getChannelCloudRecordStatsSwitch }, /* 暂不实现:501 */
    { "setChannelCloudRecordStatsSwitch",      cmd_setChannelCloudRecordStatsSwitch }, /* 暂不实现:501 */
    { "getChannelRecordingContent",            cmd_getChannelRecordingContent }, /* 暂不实现:501 */
    { "getReportServer",                       cmd_getReportServer },            /* 暂不实现:501 */
    { "getEnvironment",                        cmd_getEnvironment },             /* 暂不实现:501 */
    { "getLog",                                cmd_getLog },                     /* 暂不实现:501 */

    /* --- network 网络/时间 --- */
    { "GUI_getLocalLink",                      cmd_GUI_getLocalLink },
    { "GUI_setLocalLink",                      cmd_GUI_setLocalLink },
    { "GUI_getLanInterface",                   cmd_GUI_getLanInterface },
    { "GUI_getWanInterface",                   cmd_GUI_getWanInterface },
    { "getWanInterface",                       cmd_getWanInterface },
    { "GUI_getNetPort",                        cmd_GUI_getNetPort },
    { "GUI_setNetPort",                        cmd_GUI_setNetPort },
    { "GUI_getNTP",                            cmd_GUI_getNTP },
    { "GUI_setNTP",                            cmd_GUI_setNTP },
    { "GUI_getDDNS",                           cmd_GUI_getDDNS },
    { "GUI_setDDNS",                           cmd_GUI_setDDNS },
    { "GUI_getUPnP",                           cmd_GUI_getUPnP },
    { "GUI_setUPnP",                           cmd_GUI_setUPnP },
    { "GUI_getFTP",                            cmd_GUI_getFTP },
    { "GUI_setFTP",                            cmd_GUI_setFTP },
    { "GUI_getEmailAlert",                     cmd_GUI_getEmailAlert },
    { "GUI_setEmailAlert",                     cmd_GUI_setEmailAlert },
    { "GUI_testEmailAlert",                    cmd_GUI_testEmailAlert },
    { "GUI_getPoE",                            cmd_GUI_getPoE },
    { "GUI_setPoE",                            cmd_GUI_setPoE },

    /* --- cloud 云存 --- */
    { "X_NightOwl_setCloudRecordSwitch",       cmd_X_NightOwl_setCloudRecordSwitch },
    { "X_NightOwl_getCloudRecordSwitch",       cmd_X_NightOwl_getCloudRecordSwitch },
    { "getCloudRecordSwitch",                  cmd_getCloudRecordSwitch },
    { "setCloudRecordConfigs",                 cmd_setCloudRecordConfigs },
    { "getCloudRecordConfigs",                 cmd_getCloudRecordConfigs },
    { "getCloudRecordLogConfig",               cmd_getCloudRecordLogConfig },    /* 暂不实现 */
    { "setCloudRecordLogConfig",               cmd_setCloudRecordLogConfig },    /* 暂不实现 */
    { "startCloudRecordTest",                  cmd_startCloudRecordTest },       /* 暂不实现 */
    { "stopCloudRecordTest",                   cmd_stopCloudRecordTest },        /* 暂不实现 */
    { "getCloudRecordTestProgress",            cmd_getCloudRecordTestProgress }, /* 暂不实现 */

    /* --- record 录像/推送 --- */
    { "X_NightOwl_setChannelRecordingTriggers",           cmd_X_NightOwl_setChannelRecordingTriggers },
    { "X_NightOwl_getChannelRecordingTriggers",           cmd_X_NightOwl_getChannelRecordingTriggers },
    { "X_NightOwl_setChannelRecordingSwitch",             cmd_X_NightOwl_setChannelRecordingSwitch },
    { "X_NightOwl_getChannelRecordingSwitch",             cmd_X_NightOwl_getChannelRecordingSwitch },
    { "X_NightOwl_setChannelContinuousScheduleRecordingSwitch", cmd_X_NightOwl_setChannelContinuousScheduleRecordingSwitch },
    { "X_NightOwl_getChannelContinuousScheduleRecordingSwitch", cmd_X_NightOwl_getChannelContinuousScheduleRecordingSwitch },
    { "X_NightOwl_setChannelContinuousRecordingSchedule", cmd_X_NightOwl_setChannelContinuousRecordingSchedule },
    { "X_NightOwl_getChannelContinuousRecordingSchedule", cmd_X_NightOwl_getChannelContinuousRecordingSchedule },

    /* --- storage 存储 --- */
    { "getStorageInfo",                        cmd_getStorageInfo },
    { "X_NightOwl_getStorageInfo",             cmd_getStorageInfo },
    { "formatStorage",                         cmd_formatStorage },
    { "getAllDisksHealth",                     cmd_getAllDisksHealth },
    { "getCurrentStorage",                     cmd_getCurrentStorage },
    { "setCurrentStorage",                     cmd_setCurrentStorage },
    { "GUI_getHddConfig",                      cmd_GUI_getHddConfig },
    { "GUI_setHddConfig",                      cmd_GUI_setHddConfig },

    /* --- playback 回放/导出 --- */
    { "GUI_getPlaybackAudio",                  cmd_GUI_getPlaybackAudio },
    { "GUI_setPlaybackAudio",                  cmd_GUI_setPlaybackAudio },
    { "GUI_getFileList",                       cmd_GUI_getFileList },            /* System 域文件列表,非回放 */
    { "GUI_ChannelBackupFiles",                cmd_GUI_ChannelBackupFiles },
    { "GUI_GetChannelBackupStatus",            cmd_GUI_GetChannelBackupStatus },
    { "startRecordingBackup",                  cmd_startRecordingBackup },
    { "getRecordingBackupProgress",            cmd_getRecordingBackupProgress },
    { "GUI_StopChannelBackup",                 cmd_GUI_StopChannelBackup },
    { "GUI_getChannelEventRecordingSchedule",  cmd_GUI_getChannelEventRecordingSchedule },
    { "GUI_setChannelEventRecordingSchedule",  cmd_GUI_setChannelEventRecordingSchedule },
    { "getChannelRecordingTime",               cmd_getChannelRecordingTime },
    { "setChannelRecordingTime",               cmd_setChannelRecordingTime },

    /* --- ota 升级 --- */
    { "upgradeFirmware",                       cmd_upgradeFirmware },
    { "GUI_upgradeFirmware",                   cmd_upgradeFirmware },
    { "checkFirmwareUpgradeStatus",            cmd_checkFirmwareUpgradeStatus },
    { "GUI_checkServerFirmware",               cmd_GUI_checkServerFirmware },
    { "GUI_checkChannelServerFirmware",        cmd_GUI_checkChannelServerFirmware },
    { "GUI_upgradeChannelFirmware",            cmd_GUI_upgradeChannelFirmware },
    { "X_NightOwl_upgradeChannelFirmware",     cmd_X_NightOwl_upgradeChannelFirmware },
    { "X_NightOwl_checkChannelUpgradeStatus",  cmd_X_NightOwl_checkChannelUpgradeStatus },

    /* --- event 事件 --- */
    { "X_NightOwl_queryEventList",             cmd_X_NightOwl_queryEventList },
    { "X_NightOwl_queryEventListWithSpecificOrder", cmd_X_NightOwl_queryEventListWithSpecificOrder },
    { "X_NightOwl_queryEventCalendar",         cmd_X_NightOwl_queryEventCalendar },
    { "X_NightOwl_queryContinuousCalendar",    cmd_X_NightOwl_queryContinuousCalendar },
    { "X_NightOwl_queryRecordingInterval",     cmd_X_NightOwl_queryRecordingInterval },
    { "AI_getEventExtInfo",                    cmd_AI_getEventExtInfo },
    { "AI_getEventExtInfoBatchByReverseTime",  cmd_AI_getEventExtInfoBatchByReverseTime },
    { "AI_getEventExtInfoConfig",              cmd_AI_getEventExtInfoConfig },
    { "AI_setEventExtInfoConfig",              cmd_AI_setEventExtInfoConfig },
    { "snapshotChannel",                       cmd_snapshotChannel },
    { "X_NightOwl_getEventDownloadCapability", cmd_X_NightOwl_getEventDownloadCapability },
    { "X_NightOwl_startEventDownload",         cmd_X_NightOwl_startEventDownload },
    { "X_NightOwl_getEventDownloadProgress",   cmd_X_NightOwl_getEventDownloadProgress },
    { "X_NightOwl_startEventDownloadwithURL",  cmd_X_NightOwl_startEventDownloadwithURL },

    /* --- push 推送 --- */
    { "X_NightOwl_getChannelPushNotificationSwitch",      cmd_X_NightOwl_getChannelPushNotificationSwitch },
    { "X_NightOwl_setChannelPushNotificationSwitch",      cmd_X_NightOwl_setChannelPushNotificationSwitch },
    { "X_NightOwl_getChannelsPushNotificationSwitch",     cmd_X_NightOwl_getChannelsPushNotificationSwitch },
    { "X_NightOwl_setChannelsPushNotificationSwitch",     cmd_X_NightOwl_setChannelsPushNotificationSwitch },
    { "X_NightOwl_getChannelPushNotificationTriggers",    cmd_X_NightOwl_getChannelPushNotificationTriggers },
    { "X_NightOwl_setChannelPushNotificationTriggers",    cmd_X_NightOwl_setChannelPushNotificationTriggers },
    { "X_NightOwl_getChannelPushNotificationDoNotDisturb", cmd_X_NightOwl_getChannelPushNotificationDoNotDisturb },
    { "X_NightOwl_setChannelPushNotificationDoNotDisturb", cmd_X_NightOwl_setChannelPushNotificationDoNotDisturb },
    { "setSnooze",                             cmd_setSnooze },
    { "getSnooze",                             cmd_getSnooze },
    { "setPushPhotoSwitch",                    cmd_setPushPhotoSwitch },
    { "getPushPhotoSwitch",                    cmd_getPushPhotoSwitch },
};

const int g_nvr_cmd_table_len = (int)(sizeof(g_nvr_cmd_table) / sizeof(g_nvr_cmd_table[0]));

nvr_cmd_fn nvr_cmd_table_lookup(const char *func)
{
    if (!func) return NULL;
    for (int i = 0; i < g_nvr_cmd_table_len; i++)
        if (strcmp(g_nvr_cmd_table[i].func, func) == 0) return g_nvr_cmd_table[i].fn;
    return NULL;
}
