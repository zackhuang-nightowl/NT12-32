/***************************************************************************************
 *  nvr_cmd_table.c — 唯一权威路由表:NOP func 名 → 命名 handler(黑名单 = 本表)。
 *
 *  新增本地接口 = 在此加一行 + 在对应域文件写一个 cmd_<func>。手动加行,不自动注册。
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
    { "X_NightOwl_getDeviceCapabilities",      cmd_X_NightOwl_getDeviceCapabilities },
    { "X_NightOwl_setChannelZoomPan",          cmd_X_NightOwl_setChannelZoomPan },
    { "X_NightOwl_getChannelZoomPan",          cmd_X_NightOwl_getChannelZoomPan },

    /* --- lan 子设备 --- */
    { "GUI_LanSearch",                         cmd_GUI_LanSearch },
    { "GUI_GetAddedLanDevices",                cmd_GUI_GetAddedLanDevices },
    { "GUI_getLanDevice",                      cmd_GUI_getLanDevice },
    { "GUI_setLanDevice",                      cmd_GUI_setLanDevice },
    { "GUI_LanAddDevice",                      cmd_GUI_LanAddDevice },
    { "GUI_LanDelDevice",                      cmd_GUI_LanDelDevice },

    /* --- system 设备/账户 --- */
    { "setName",                               cmd_setName },
    { "getName",                               cmd_getName },
    { "getDeviceInfo",                         cmd_getDeviceInfo },
    { "X_NightOwl_setTimezone",                cmd_X_NightOwl_setTimezone },
    { "X_NightOwl_getTimezone",                cmd_X_NightOwl_getTimezone },
    { "set_datetime",                          cmd_set_datetime },
    { "X_NightOwl_setTimeSyncSwitch",          cmd_X_NightOwl_setTimeSyncSwitch },
    { "X_NightOwl_getTimeSyncSwitch",          cmd_X_NightOwl_getTimeSyncSwitch },
    { "reboot",                                cmd_reboot },
    { "X_NightOwl_setOwner",                   cmd_X_NightOwl_setOwner },
    { "X_NightOwl_getOwner",                   cmd_X_NightOwl_getOwner },
    { "GUI_getRemoteAccessState",              cmd_GUI_getRemoteAccessState },
    { "GUI_setRemoteAccessState",              cmd_GUI_setRemoteAccessState },

    /* --- cloud 云存 --- */
    { "X_NightOwl_setCloudRecordSwitch",       cmd_X_NightOwl_setCloudRecordSwitch },
    { "X_NightOwl_getCloudRecordSwitch",       cmd_X_NightOwl_getCloudRecordSwitch },
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
    { "X_NightOwl_getStorageInfo",             cmd_getStorageInfo },   /* 同实现 */
    { "formatStorage",                         cmd_formatStorage },
    { "getAllDisksHealth",                     cmd_getAllDisksHealth },
    { "getCurrentStorage",                     cmd_getCurrentStorage },
    { "setCurrentStorage",                     cmd_setCurrentStorage },

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
