# 接口硬编码默认值审计

> 原则：**GET 应答里的「状态/配置/统计」只能来自 (1) NVR 本地持久化 或 (2) IPC 设备查询**；handler 内不得用字面量/`NVR_DEF_*` 冒充真实值。
> 审计日期：2026-08-20（cap 单机桩清理后修订）。范围：`g_nvr_cmd_table` LOCAL handler + `nvr_settings_*` 读路径 + ONVIF mapping 入口 cap。

**图例**

| 标记 | 含义 |
|------|------|
| 🔴 | 函数内硬编码/全零桩，非 DB 非 IPC |
| 🟠 | DB 无行时在 C 层合成默认（应只读已存行或明确错误） |
| 🟡 | 读库时用 `nvr_settings_get_*`/`NVR_DEF_*` 作 fallback |
| 🟢 | 能力/协议声明（描述本机支持什么，非通道状态）— 若也要入库则另议 |
| ✅ | 已接真实源（identity/settings/通道/ONVIF 透传） |

---

## 一、LOCAL 表：产品不需要 → **501**（✅ 已清理 cap 假数据）

下列命令在 `g_nvr_cmd_table` 有行，`nvr_cmd_misc.c` / `nvr_cmd_system.c` 直接 **`nvr_resp_not_support()`**；原 `components/nop` cap 单机内存桩已删除。

| 命令 | 现状 |
|------|------|
| `getChannelStats` / `getChannelLoading` | 501 |
| `getCloudStatusHistory` / `getChannelCloudRecordStats` / `get/setChannelCloudRecordStatsSwitch` | 501 |
| `getChannelRecordingContent` | 501 |
| `getLog` / `GUI_getSystemLog` / `getReportServer` / `getEnvironment` | 501 |
| `startCloudRecordTest` / `stopCloudRecordTest` / `getCloudRecordTestProgress` | 501（`nvr_cmd_cloud.c`） |
| `getCloudRecordLogConfig` / `setCloudRecordLogConfig` | 501（`nvr_cmd_cloud.c`） |

---

## 二、LOCAL handler：应答字段硬编码（🔴/🟠/🟡）

### 2.1 设备 / 系统（`nvr_cmd_system.c`）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `getName` | `name` | 🟡 `nvr_settings_get_str(..., NVR_DEF_NAME)` | settings 或空 |
| `getDeviceInfo` | `name` | 🟡 同上 | settings |
| | `model` | 🟡 `NVR_DEF_MODEL` | `/User/OWLModel` → identity |
| | `firmwareVersion` | 🟡 `NVR_DEF_FW_VERSION` | identity/编译常量单点 |
| | `channels` | 🟡 `NVR_DEF_CAPACITY` | settings `system.capacity` |
| | `sn`/`mac` | ✅ | `nvr_identity_*` |
| `X_NightOwl_getTimezone` | `timezone`/`tz_dst` | 🟡 `NVR_DEF_TIMEZONE` / `""` | settings |
| `X_NightOwl_getTimeSyncSwitch` | `enable` | 🟡 默认 1 | settings |
| `getAvPassword` | `value` | 🔴 空则 `"888888"` | `/User/OWL/tutkdata.json` only |
| `getAvAccount` | `value` | 🔴 固定 `"admin"` | identity 或 501 |
| `getCurrentClouds` | `currentCloud`/`availableClouds` | ✅ | `system.json` → KV `cloudServer.current` / `cloudServer.available` |
| `GUI_getFeatureList` | `Restore[]` | 🔴 静态数组 General/Encode/... | 产品配置表 |
| `GUI_getRemoteAccessState` | `enabled` | 🟡 默认 0 | settings |
| `GUI_getAutoRebootSetting` | 各字段 | 🟡 默认 0 | settings |

### 2.2 显示（`nvr_cmd_display.c`）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `GUI_getDeviceDisplayMode` | mode/page | 🟡 `NVR_DEF_GUI_MODE/PAGE` | GUI_CONFIG / settings |
| | firstView/autoChange/autoEvent | 🟡 默认 1/-1/0 | settings |
| `GUI_getSysDisplay` | `resolution` | 🟡 默认 `"1920x1080"` | settings + mhal 实际 |
| | `displayOpacity` | 🔴 255 | settings 或省略 |
| | `fb` | 🔴 `"fb1"` | 板级配置 |
| `X_NightOwl_getDeviceCapabilities` | `signal` | 🔴 缺则 `"IPC"` | `camera_capability.signal`（DB 播种为 IPC，应用已存值） |
| | `device.capabilities[]` | 🟢 代码列举 displayMode/BLE/... | 产品能力表（可接受写死若与真机一致） |
| `GUI_longPolling` | （无假 channel 列表） | ✅ 事件/录像/notify 来自运行时 | — |
| `getCableConnectStatus` | HDMI/VGA | ✅ | DRM `/sys/class/drm/.../status`（`mhal_vout_get_cable_connect`） |

### 2.3 网络（`nvr_cmd_network.c`）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `GUI_getNetPort` | Http/Https/TCP/Rtsp | 🟡 80/443/8089/554 | settings `network.port.*` |
| `GUI_getNTP` | ServerName | 🟡 `"pool.ntp.org"` | settings `system.ntp` |
| | UpdatePeriod/Port | 🔴 100 / 123 | settings 或省略 |
| | ServerOption 1/2/3 | 🔴 三个公共 NTP 域名 | settings 列表 |
| `GUI_getEmailAlert` | 默认值 | 🔴 `email_defaults()` gmail/465 | settings 行或空 |
| `GUI_getLanInterface` | total/maxRx | ✅ 读链路 | — |
| | allocatedRxBandwidth | 🔴 **未返回**（注释待实现） | 流统计 |
| `GUI_getDDNS/FTP/UPnP` | — | ✅ 读 settings / 系统 | — |

### 2.4 录像 / 推送（`nvr_cmd_record.c` / `nvr_cmd_push.c`）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `X_NightOwl_getChannelRecordingSwitch/Triggers` | 无 DB 行 | ✅ 只读 `record_config`；无行→`no_config` | **仅** `record_config` 行（seed 32 行） |
| `record_config.stream_type` | 本地写盘 | ✅ | `rec_schedule_apply` → `nvr_stream_set_record_mask`（出厂 **both**） |
| `getCloudRecordConfigs` / `setCloudRecordConfigs` | streamType | ✅ | 读/写 `cloud_channel`；SET 缺省 **sub**；上传 `rsdk_group_query_stream` |
| `X_NightOwl_getChannelContinuousScheduleRecordingSwitch` | sched_on | ✅ 只读 `record_schedule`；无行→`no_config` | `record_schedule` 行 |
| `getChannelRecordingTime` | value | ✅ KV `record.ch.N.post_s`；无 KV→`no_config` | settings KV（seed post_s=10） |
| `X_NightOwl_getChannelPushNotificationDoNotDisturb` | start/end/timeUnit | ✅ 只读 push_config 列 | push_config 行（seed 2100/0700/hour） |
| 推送 triggers GET | `[]` | ✅ DB 空 triggers | push_config |

### 2.5 通道 / 云存 / 存储（misc / cloud / storage）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `X_NightOwl_getChannelInfo` | `storageType` | 🔴 `"none"` | IPC GetStorageConfiguration 或省略 |
| | `type` | 🔴 启发式 doorbell | IPC 型号/cap |
| | `manufacturer` | 🔴 MAC 前缀 NightOwl | IPC |
| | `network` | 🔴 PoE=`Ethernet` else WiFi 文案 | IPC `getCurrentWifi`（已有 `wifi_fill`） |
| | `signalStrength` | 🟡 PoE 固定 0 | IPC |
| `getCloudRecordConfigs` | `mode` | ✅ 读 `group`/`storage.has_disk` | 有盘 async / 无盘 sync |
| | `streamType` 每通道 | ✅ | `cloud_channel.stream_type`（出厂 sub） |
| `getCurrentStorage` | — | ✅ settings | — |
| `set/getHddConfig` | hdd_full | 🟡 默认 `"overwrite"` | settings |
| `getStorageInfo` | 容量 | ✅ rsdk probe | — |
| `getAllDisksHealth` | SMART | ✅ rsdk_smart | — |

### 2.6 P2P / 回放 / 事件（`nvr_cmd_p2p.c` 等）

| 命令 | 字段 | 现状 | 应来自 |
|------|------|------|--------|
| `getLiveCapabilities` / `getPlaybackCapabilities` | protocol/streamType 列表 | 🟢 固定 rtsp-iotc-tunnel + 5 种 stream | 本机能力（非 IPC 状态） |
| | channels | ✅ 在线通道 | 通道状态机 |
| `getSpeakerCapabilities` | sampleRate=8000, mono | 🟢 本机 talk 能力 | talk 模块 |
| `buildTunnel` | username | 🔴 `"Tutk.com"` | TUTK 文档常量 |
| | iotc-channel | 🔴 2 | 配置 |
| `X_NightOwl_getEventDownloadCapability` | produceMode/container | 🔴 dynamic/mp4 | 本机导出能力 |
| `GUI_getChannelEventRecordingSchedule` | rule id/time | ✅ 只读 schedule 行原值 | schedule 表行 |

---

## 三、`nvr_settings_*` 读路径（通道表 + KV）

| API | 状态 | 说明 |
|-----|------|------|
| `nvr_settings_rec_sched_get` | ✅ | 只读行；无行返回 `<0` |
| `nvr_settings_push_get` | ✅ | 只读 push_config 列；无行返回 `<0` |
| `nvr_settings_record_post_s_get` / `pre_s_get` | ✅ | 只读 KV `record.ch.N.post_s/pre_s`；无键返回 `-1`；seed 写入 10/5 |
| `nvr_settings_caps_get` | ✅ | 无行不填 `signal_out` 默认 IPC |
| `nvr_settings_get_int/str` 各处 | 🟡 待改 | `NVR_DEF_*` / 80/443/554 — 系统/网络 GET 仍带 fallback |

**出厂默认值**在 `seed_chn_defaults()` / `seed_from_json()` **写 DB 一次**；通道域 GET 只 `SELECT`，读不到返回 `no_config`。

---

## 四、cap 层（NVR 保留部分）

`components/nop` cap **仅保留 ONVIF mapping 入口**（`cap_misc_ext`：OSD/隐私区/sensor config；`cap_ai` / `cap_ai_advanced`：AI/活动区/线域等）。单机相机内存桩（`cap_cloud` / `cap_agent` 全文件、原 `cap_misc_ext` 统计/鉴权/zoom 等）已删除。

未进 `g_nvr_cmd_table` 且无 `channel` 的命令 → `fallback_nop` → 无 handler 则 **501**。

**有 `channel` 时**：优先透传 IPC 或 ONVIF mapping（✅ 真实源）。

---

## 五、应来自 IPC（透传 / ONVIF）— 本地不得伪造

| 类型 | 路由 | 说明 |
|------|------|------|
| NOP 相机命令 | `backend==0` → `/APPJsonCmd` | ✅ 真实 IPC JSON |
| ONVIF 映射命令 | `mappingonvif` | ✅ SOAP → IPC |
| `AI_getChannelAICapabilities` (NOP) | 透传 | ✅ |
| `snapshotChannel` | ONVIF GetSnapshot | ✅ |
| `getEnhancedSecurity` / `setEnhancedSecurity` (NOP) | `nvr_chan_enh_*` → IPC | ✅ |
| cap 内 `X_NightOwl_get/setChannelZoomPan/OSD/...` | ✅ LOCAL 或 ONVIF mapping | NVR 无 cap 内存回落 |

**注意**：`X_NightOwl_set/getChannelZoomPan` 在 LOCAL 表接 `mhal_vout_set_crop`；cap 同名 handler 已删。

---

## 六、DB 播种 vs 函数硬编码（边界）

| 位置 | 内容 | 是否合规 |
|------|------|----------|
| `seed_chn_defaults()` | 32 行 record/push/schedule/cloud/schedule_event | ✅ 本地存储初始值 |
| `record_config.stream_type='both'` | 驱动 streaming 主/子 writer | ✅ 已接 `nvr_record_policy` |
| `cloud_channel.stream_type='sub'` | 驱动云存登记+上传轨 | ✅ 已接 uploader / record_sched |
| `camera_capability.signal='IPC'` 占位 | 空通道占位 | 🟡 空口不应出现在 GET 列表；有设备时应 UPDATE 为真实 signal |
| handler GET 再填 7×24 / record_on=1 | 与 seed 重复 | ✅ 已删 handler 合成（通道域只读 DB） |

---

## 七、清理优先级建议

1. ~~**P0**：§一 NVR 不需要的命令 → 501~~ ✅ 2026-08-20
2. **P1**：§2.1–2.5 剩余 🔴 字段（`888888`/`admin`/channel info 伪造字段）。
3. **P2**：`nvr_settings_*` GET 去掉 C 层 fallback；handler 读失败返回 err 而非合成。
4. **P3**：🟢 能力列表是否迁入 settings/产品 JSON（可选）。

---

## 附：`g_nvr_cmd_table` 标注「暂不实现:501」的项

`getChannelStats` · `getChannelLoading` · `getCloudStatusHistory` · `getChannelCloudRecordStats*` · `getChannelRecordingContent` · `getReportServer` · `getEnvironment` · `getLog` · `GUI_getSystemLog` · 云存 test/log 五命令 — 均 **501**，不再回落 cap。
