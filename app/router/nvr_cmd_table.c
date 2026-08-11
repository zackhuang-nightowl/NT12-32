/***************************************************************************************
 *  nvr_cmd_table.c — 唯一权威路由表:NOP func 名 → 命名 handler(黑名单 = 本表)。
 *
 *  新增本地接口 = 在此加一行 + 在对应域文件写一个 cmd_<func>。手动加行,不自动注册。
 *  无 NVR 真实实现、仅 cap 回落的行用行尾注释「待做:...」标注。
 ***************************************************************************************/
#include "nvr_cmd_internal.h"
#include <string.h>

const nvr_cmd_route_t g_nvr_cmd_table[] = {
    /* --- display 出图 --- */
    { "GUI_setDeviceDisplayMode",              cmd_GUI_setDeviceDisplayMode },
    { "GUI_getDeviceDisplayMode",              cmd_GUI_getDeviceDisplayMode },
    { "GUI_setChannelMapping",                 cmd_GUI_setChannelMapping },
    { "GUI_getChannelMapping",                 cmd_GUI_getChannelMapping },
    { "GUI_setDeviceDisplayExt",               cmd_GUI_setDeviceDisplayExt },
    { "GUI_getDeviceDisplayExt",               cmd_GUI_getDeviceDisplayExt },
    { "GUI_getSysDisplay",                     cmd_GUI_getSysDisplay },
    { "GUI_setSysDisplay",                     cmd_GUI_setSysDisplay },
    { "X_NightOwl_getChannelStatus",           cmd_X_NightOwl_getChannelStatus },
    { "GUI_longPolling",                       cmd_GUI_longPolling },
    { "GUI_playbackControl",                   cmd_GUI_playbackControl },
    { "GUI_setPlaybackMode",                   cmd_GUI_setPlaybackMode },
    { "GUI_getPlaybackMode",                   cmd_GUI_getPlaybackMode },
    { "getPlaybackCapabilities",               cmd_getPlaybackCapabilities },
    { "X_NightOwl_getDeviceCapabilities",      cmd_X_NightOwl_getDeviceCapabilities },
    { "AI_getChannelAICapabilities",           cmd_AI_getChannelAICapabilities },
    { "X_NightOwl_setChannelZoomPan",          cmd_X_NightOwl_setChannelZoomPan },
    { "X_NightOwl_getChannelZoomPan",          cmd_X_NightOwl_getChannelZoomPan },

    /* --- lan 子设备 --- */
    { "GUI_LanSearch",                         cmd_GUI_LanSearch },
    { "GUI_GetAddedLanDevices",                cmd_GUI_GetAddedLanDevices },
    { "GUI_getLanDevice",                      cmd_GUI_getLanDevice },
    { "GUI_setLanDevice",                      cmd_GUI_setLanDevice },
    { "GUI_LanAddDevice",                      cmd_GUI_LanAddDevice },
    { "GUI_LanDelDevice",                      cmd_GUI_LanDelDevice },

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
    { "X_NightOwl_setTimeSyncSwitch",          cmd_X_NightOwl_setTimeSyncSwitch },
    { "X_NightOwl_getTimeSyncSwitch",          cmd_X_NightOwl_getTimeSyncSwitch },
    { "reboot",                                cmd_reboot },
    { "X_NightOwl_resetToFactorySettings",     cmd_X_NightOwl_resetToFactorySettings },
    { "X_NightOwl_setOwner",                   cmd_X_NightOwl_setOwner },
    { "X_NightOwl_getOwner",                   cmd_X_NightOwl_getOwner },
    { "GUI_getRemoteAccessState",              cmd_GUI_getRemoteAccessState },
    { "GUI_setRemoteAccessState",              cmd_GUI_setRemoteAccessState },
    { "GUI_getFeatureList",                    cmd_GUI_getFeatureList },
    { "getIotcAuthKey",                        cmd_getIotcAuthKey },
    { "setIotcAuthKey",                        cmd_setIotcAuthKey },
    { "GUI_getUID",                            cmd_GUI_getUID },
    { "GUI_getAutoRebootSetting",              cmd_GUI_getAutoRebootSetting },
    { "GUI_setAutoRebootSetting",              cmd_GUI_setAutoRebootSetting },
    { "GUI_getSystemLog",                      cmd_GUI_getSystemLog },           /* 待做:NVR 日志 */

    /* --- account 鉴权 --- */
    { "GUI_login",                             cmd_GUI_login },                  /* 待做:auth */
    { "GUI_logout",                            cmd_GUI_logout },                 /* 待做:auth */
    { "GUI_LoginPage",                         cmd_GUI_LoginPage },              /* 待做:auth */
    { "GUI_getLoginStatus",                    cmd_GUI_getLoginStatus },         /* 待做:auth */
    { "GUI_createUser",                        cmd_GUI_createUser },             /* 待做:auth */
    { "GUI_deleteUser",                        cmd_GUI_deleteUser },             /* 待做:auth */
    { "GUI_getUsers",                          cmd_GUI_getUsers },               /* 待做:auth */
    { "GUI_getUserGroupPermissions",           cmd_GUI_getUserGroupPermissions },/* 待做:auth */
    { "GUI_forgetPassword",                    cmd_GUI_forgetPassword },         /* 待做:auth */

    /* --- misc 通道聚合/安全 --- */
    { "getChannelsStatus",                     cmd_getChannelsStatus },
    { "getChannelStats",                       cmd_getChannelStats },            /* 待做:通道统计 */
    { "getChannelLoading",                     cmd_getChannelLoading },          /* 待做:加载状态 */
    { "getEnhancedSecurity",                   cmd_getEnhancedSecurity },        /* 待做:NVR 编排+crypto */
    { "setEnhancedSecurity",                   cmd_setEnhancedSecurity },        /* 待做:NVR 编排+crypto */
    { "X_NightOwl_getDeviceActive",            cmd_X_NightOwl_getDeviceActive }, /* 待做:设备激活 */
    { "X_NightOwl_setDeviceActive",            cmd_X_NightOwl_setDeviceActive }, /* 待做:设备激活 */
    { "getCurrentClouds",                      cmd_getCurrentClouds },           /* 待做:云存状态 */
    { "getCloudStatusHistory",                 cmd_getCloudStatusHistory },      /* 待做:云存历史 */
    { "getChannelCloudRecordStats",            cmd_getChannelCloudRecordStats }, /* 待做:云存统计 */
    { "getChannelCloudRecordStatsSwitch",      cmd_getChannelCloudRecordStatsSwitch }, /* 待做 */
    { "setChannelCloudRecordStatsSwitch",      cmd_setChannelCloudRecordStatsSwitch }, /* 待做 */
    { "getChannelRecordingContent",            cmd_getChannelRecordingContent }, /* 待做:recorder */
    { "getReportServer",                       cmd_getReportServer },            /* 待做:配置 */
    { "getEnvironment",                        cmd_getEnvironment },             /* 待做:配置 */
    { "getLog",                                cmd_getLog },                     /* 待做:NVR 日志 */

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

    /* --- record 录像/推送 --- */
    { "X_NightOwl_setChannelRecordingTriggers",           cmd_X_NightOwl_setChannelRecordingTriggers },
    { "X_NightOwl_getChannelRecordingTriggers",           cmd_X_NightOwl_getChannelRecordingTriggers },
    { "X_NightOwl_setChannelsPushNotificationSwitch",     cmd_X_NightOwl_setChannelsPushNotificationSwitch },
    { "X_NightOwl_getChannelsPushNotificationSwitch",     cmd_X_NightOwl_getChannelsPushNotificationSwitch },
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
    { "GUI_StopChannelBackup",                 cmd_GUI_StopChannelBackup },
    { "GUI_getChannelEventRecordingSchedule",  cmd_GUI_getChannelEventRecordingSchedule }, /* 待做:schedule */
    { "GUI_setChannelEventRecordingSchedule",  cmd_GUI_setChannelEventRecordingSchedule }, /* 待做:schedule */
    { "getChannelRecordingTime",               cmd_getChannelRecordingTime },    /* 待做:recorder */

    /* --- ota 升级 --- */
    { "upgradeFirmware",                       cmd_upgradeFirmware },
    { "checkFirmwareUpgradeStatus",            cmd_checkFirmwareUpgradeStatus },

    /* --- event 事件 --- */
    { "X_NightOwl_queryEventList",             cmd_X_NightOwl_queryEventList },
    { "X_NightOwl_queryEventCalendar",         cmd_X_NightOwl_queryEventCalendar },
    { "X_NightOwl_queryContinuousCalendar",    cmd_X_NightOwl_queryContinuousCalendar },
    { "X_NightOwl_queryRecordingInterval",     cmd_X_NightOwl_queryRecordingInterval },
};

const int g_nvr_cmd_table_len = (int)(sizeof(g_nvr_cmd_table) / sizeof(g_nvr_cmd_table[0]));

nvr_cmd_fn nvr_cmd_table_lookup(const char *func)
{
    if (!func) return NULL;
    for (int i = 0; i < g_nvr_cmd_table_len; i++)
        if (strcmp(g_nvr_cmd_table[i].func, func) == 0) return g_nvr_cmd_table[i].fn;
    return NULL;
}
