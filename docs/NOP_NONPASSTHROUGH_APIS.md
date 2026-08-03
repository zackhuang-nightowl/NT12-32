# NOP 命令路由 — 非透传 API 名单（活文档）

> **NVR 不是所有 NOP 接口都透传。** 本表是 NOP 路由前门的查表依据，**边实现边补充**：
> 每实现/确认一个接口，就在下表登记「命令 / 分类 / NVR 侧数据来源 / 状态 / 备注」。
> 相关：即插即用连接流程见 [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md)。

## 路由三档决策

给定一条 App 发来的 NOP 命令，前门按顺序判定：

1. **非透传（LOCAL）** — 整机/NVR 级命令，由 NVR 自己的 handler 应答，**不发给相机**。
2. **透传（PASSTHRU）** — 面向某相机通道、且该通道 `kind=NOP` → 原样转发 NOP 给相机。
3. **翻译（TRANSLATE）** — 面向某相机通道、且 `kind=nopOnvif|onvif` → `nop_onvif_map_dispatch` 转 ONVIF。

**默认规则**：不在本表、且带 `channel` 参数 → 按「面向通道 → PASSTHRU/TRANSLATE」；不带 `channel`
且属整机语义 → 倾向 LOCAL。不确定的登记为 `TBD` 并在实现时确认。

状态图例：`✔`=已实现并确认 · `~`=已归类待接线 · `?`=分类待确认(TBD)

---

## A. 非透传 LOCAL（NVR 本地处理）

`GUI_*` 前缀命令基本都是本地 HMI/整机语义 → LOCAL。

### A1. 设备 / 系统 / 能力
| 命令 | 状态 | NVR 侧数据来源 | 备注 |
|---|---|---|---|
| `getDeviceInfo` | ~ | NVR 身份(SN/MAC/型号/FW) | 工厂区 + 设置库 |
| `X_NightOwl_getDeviceCapabilities` | ~ | NVR 聚合 device+channels 能力 | 含 `cloudRecording` |
| `getName` / `GUI_getSysDisplay` / `GUI_getFeatureList` | ~ | 设置库 | |
| `getIotcAuthKey` / `GUI_getUID` | ~ | TUTK 配置 | |
| `GUI_getAutoRebootSetting` / `GUI_setAutoRebootSetting` | ~ | 设置库 | 周维护 |
| `GUI_getSystemLog` / `getLog` / `getLogs` | ? | NVR 日志 | |

### A2. 账户 / 鉴权（本地 Admin + NOP owner）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_login` / `GUI_logout` / `GUI_LoginPage` / `GUI_getLoginStatus` | ~ | 设置库 `auth` | 锁定/超时 |
| `GUI_createUser` / `GUI_deleteUser` / `GUI_getUsers` / `GUI_getUserGroupPermissions` | ~ | 设置库 | |
| `GUI_forgetPassword` | ? | 设置库 | 找回流程 |
| `X_NightOwl_setOwner` / `X_NightOwl_getOwner` | ~ | 设置库 `nop_owner`(stoken) | 云存凭据，非易失 |

### A3. LAN Add / 发现 / 即插即用（见 BIND_IPC_FLOW）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_LanSearch` | ~ | 发现(WS-Disc/34569) | 5s 回复 |
| `GUI_LanAddDevice` | ~ | 通道绑定 | 60s 超时 |
| `GUI_LanDelDevice` | ~ | 通道表 | 仅本地删 |
| `GUI_GetAddedLanDevices` | ~ | 通道表 + 状态 | |
| `GUI_getLanDevice` / `GUI_setLanDevice` | ~ | 通道/增强配置 | config 弹窗 |
| `getEnhancedSecurity` / `setEnhancedSecurity` | ~ | NVR 编排(对相机代执行) | 密码算法 §crypto |
| `X_NightOwl_getDeviceActive` / `X_NightOwl_setDeviceActive` | ~ | NVR 编排 + 设置库存密码 | AES256 激活 |
| `GUI_getChannelMapping` / `GUI_setChannelMapping` | ~ | 通道表 | |
| `getChannelsStatus` / `getChannelStats` / `getChannelLoading` | ~ | 通道状态机 | 聚合 |

### A4. 存储
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_getStorageInfo` / `GUI_getHddConfig` | ~ | `components/storage` | |
| `formatStorage` | ~ | storage format | |
| `getCurrentStorage` / `setCurrentStorage` | ~ | storage | |
| `getAllDisksHealth` | ~ | storage health(SMART) | |

### A5. 云存（NVR 本地，见 cap_cloud）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_setCloudRecordSwitch` / `getCloudRecordSwitch` | ~ | 设置库 `cloud.switch` | 门控上传器 |
| `getCloudRecordConfigs` / `setCloudRecordConfigs` | ~ | 设置库 `cloud_channel` | mode 只读 |
| `getChannelCloudRecordStats*` / `getCloudStatusHistory` / `getCurrentClouds` | ? | rsdk_cloud 状态 | |

### A6. 录像 / 回放（来自 NVR 盘）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_getChannelEventRecordingSchedule` / `GUI_setChannelEventRecordingSchedule` | ~ | 设置库 `rec_schedule` | |
| `getChannelRecordingTime` / `getChannelRecordingContent` | ~ | recorder 索引 | |
| `GUI_getPlaybackMode` / `GUI_playbackControl` / `GUI_getPlaybackAudio` | ~ | recorder 回放 | |
| `GUI_getFileList` / `GUI_ChannelBackupFiles` / `GUI_GetChannelBackupStatus` | ~ | recorder 索引/导出 | |
| 事件/日历查询（queryEventList/Calendar 等） | ~ | nop_event_hub + meta | NVR 汇总 |

### A7. 网络 / 时间 / 显示（整机）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_getLanInterface` / `GUI_getWanInterface` / `getWanInterface` / `GUI_getNetPort` | ~ | 设置库 `netif` | |
| `GUI_getNTP` / `GUI_setNTP` / `GUI_getDDNS` / `GUI_getUPnP` / `GUI_getFTP` / `GUI_getEmailAlert` | ~ | 设置库 | |
| `GUI_getPoE` | ~ | PoE 状态 | 串口 MCU 可选 |
| `GUI_getDeviceDisplayMode` / `GUI_setDeviceDisplayMode` / `GUI_getSysDisplay` | ~ | preview/vout | |
| `GUI_longPolling` | ~ | 事件/状态推送 | 长轮询 |
| `getReportServer` / `getEnvironment` | ? | 配置 | |

---

## B. 透传 PASSTHRU / 翻译 TRANSLATE（面向相机通道）

带 `channel` 的相机侧控制：`kind=NOP` 透传；`kind=nopOnvif|onvif` 走 `nop_onvif_map_dispatch`
（映射层 9 域已完成，见 [nop-onvif-mapping-layer]）。

| 命令(族) | 状态 | 备注 |
|---|---|---|
| PTZ：`ptzMove/ptzByStep/ptzStop/GotoPreset` / `getPtzPresets/getPtzPatrols/getPtzTrack` | ~ | §2 PTZ 映射 |
| OSD：`X_NightOwl_getOSD` / `X_NightOwl_setOSD` | ~ | §5 OSD 映射 |
| 隐私区：`X_NightOwl_get/setChannelPrivacyZone` | ~ | §7 Privacy 映射 |
| 灯：`X_NightOwl_setChannelLightSwitch` / `getChannelIndicatorLightSwitch` / floodlight 族 | ~ | 相机能力 |
| 音频告警：`setChannelAudioAlert` / speaker/mic 族 | ~ | 相机能力 |
| AI 传感器：`get/setChannelSensorConfig` / `getChannelSensorLinkage` | ~ | §8/§9 AI 映射 |
| 媒体档：`GUI_getChannelMediaProfiles`(相机档)/ `getProfile` | ? | 区分 NVR 档 vs 相机档 |

> ⚠️ 注意易混：`getChannel*` 里既有**相机设置**（→PASSTHRU/TRANSLATE，如 SensorConfig/FloodLight/
> LightSwitch），也有 **NVR 聚合**（→LOCAL，如 ChannelsStatus/ChannelMapping/RecordingTime/
> ChannelEventRecordingSchedule）。逐条实现时在本表定档。

---

## 变更记录
- 初始种子：由计划 §B0.1 + 现有 `components/nop/src/business/caps/*` 命令名归类而成。
