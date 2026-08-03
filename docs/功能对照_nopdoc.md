# 功能对照（依据 nop_doc 权威文档）— 供逐项判断

> 依据 `NT12-SDK/nop_doc/`（camera + videoRecorder 公用）逐项梳理。camera 侧命令是 NVR 收界面 NOP 后
> **透传/翻译给相机**的；videoRecorder 侧是 **NVR 本地**处理的。最后一列 **你判断** 留空给你定。
>
> **列含义**：
> - **handler**：nopcore(293 funcs) 里是否已注册该命令处理器（✓有 / ✗无）。
> - **我判断**：`✅ 已接真实子系统`(本固件跑) · `🟡 handler在但未接真实子系统/待接` · `⤳ 透传或翻译给相机`(机制✅+映射✅，取决路由接线与相机) · `❌ 无handler/未实现`。
> - camera 侧「⤳」项：NOP↔ONVIF 映射(§核心 M1)已完成，逐通道 backend 打标✅；真正生效取决于相机硬件与路由接线。

---

# Part 1 — NVR 本地功能（videoRecorder）

## 1.1 AddCamera 相机接入 / 即插即用 / 激活 / 增强
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| ONVIF WS-Discovery(Hello/Probe/Scopes) | (ONVIF) | — | 🟡 | `nvr_onvif_discover` 结构在；探测循环未在 app 驱动 | |
| 设备三分类 NOP/nopOnvif/onvif | (Scopes 解析) | — | ✅ | `nvr_dev_classify`(16 自测)+serial 提取 | |
| 取流 URL(GetStreamUri 主/子) | (ONVIF) | — | ✅ | `nvr_onvif_get_url` | |
| 通道在线状态机+断线退避重连 | — | — | ✅ | `nvr_channel.c` | |
| PoE 口→通道 即插即用绑定 | — | — | 🟡 | `nvr_chan_bind_poe`在；发现循环+eth1 VLAN/DHCP(系统级)未接 | |
| 能力查询 getCapabilities(/netsdk) | `getCapabilities`(HTTP) | — | 🟡 | 对相机 HTTP GET；接线 | |
| 时间同步 SetSystemDateAndTime | (ONVIF) | — | 🟡 | 接线 | |
| LAN Add 检索 | `GUI_LanSearch` | ✓ | 🟡 | handler在，未接发现/通道管理 | |
| LAN Add 添加/删除 | `GUI_LanAddDevice`/`GUI_LanDelDevice` | ✓ | 🟡 | 同上 | |
| LAN 已加列表/配置 | `GUI_GetAddedLanDevices`/`GUI_getLanDevice`/`GUI_setLanDevice` | ✓ | 🟡 | 同上 | |
| 增强安全模式 查询/设置 | `getEnhancedSecurity`/`setEnhancedSecurity` | ✓ | 🟡 | handler在；密码算法 P_enh **已实现**(密钥待填)，连接握手未在 app 驱动 | |
| 设备激活 查询/执行 | `X_NightOwl_getDeviceActive`/`X_NightOwl_setDeviceActive` | ✓ | 🟡 | handler在；P_act+AES256密文 **已实现**(盐待填)，激活流程未在 app 驱动 | |

## 1.2 Storage 存储
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 录像引擎(裸盘/环形/索引/AES/多盘/导出/元数据/抓拍) | — | — | ✅ | recorder librsdk(8 example PASS) | |
| 盘发现/格式化编排/装配/热插拔/防挂载 | — | — | ✅ | `storage`(SMART 明细待接平台) | |
| 存储信息 | `X_NightOwl_getStorageInfo` | ✓ | 🟡 | handler在;接真实盘状态需接线 | |
| 格式化 | `formatStorage` | ✓ | 🟡 | handler在;接 storage_format 需接线 | |
| 当前存储 查询/设置 | `getCurrentStorage`/`setCurrentStorage` | ✓ | 🟡 | 接线 | |
| 盘健康 SMART | `getAllDisksHealth` | ✓ | 🟡 | handler在;SMART 明细待接平台 | |

## 1.3 cloudRec 云存
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 事件切片上传引擎(取段→TS→VSaaS→回写) | — | — | ✅ | `cloud_uploader`，按设置门控启动 | |
| 录像云存状态内置 recorder | — | — | ✅ | `rsdk_cloud`(20 自测) | |
| 多通道 starttime 埋通道 / Update Tags / -1002~4 强制关 | — | — | ✅ | `uploader.c` | |
| 云存开关 查询/设置 | `X_NightOwl_get/setCloudRecordSwitch` | ✓ | 🟡 | 设置库 cloud.switch 在;cap_cloud→设置库+门控上传器 待接 | |
| 云存配置 查询/设置 | `getCloudRecordConfigs`/`setCloudRecordConfigs` | ✓ | 🟡 | 设置库 cloud_channel 表在;cap_cloud 待接 | |
| 账主/stoken 查询/设置 | `X_NightOwl_setOwner`/`getOwner` | ✓ | 🟡 | 设置库 nop_owner 表在;cap_misc 仍静态量,待接 | |
| 云存连接测试 | `getCloudRecordTestProgress`/`startCloudRecordTest` | ✓/✗ | 🟡 | 进度 handler在;start 无 handler | |
| 云存统计 | `getChannelCloudRecordStats`/`setChannelCloudRecordStatsSwitch` | ✓ | 🟡 | 接 rsdk_cloud 统计 | |
| 同步(无盘)上传 | — | — | ❌ | v2 延后 | |

## 1.4 GUI 实时预览
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 分屏 1/4/6/9/12/16 | — | — | ✅ | `nvr_preview`(6/12 用 9/16 网格) | |
| 双码流(主+子) | — | — | ✅ | streaming 每通道主/子 | |
| 单画面/全屏切主码流·放大 | — | — | ✅ | fullscreen/single_zoom | |
| 轮巡(10s) | — | — | 🟡 | shuffle 结构在,定时逻辑待补 | |
| 每通道音频开关 | — | — | 🟡 | 结构在,音频路径待接 | |
| 实时状态图标(motion/human/face/录像) | — | — | ✅ | 事件→set_icons | |
| 直播能力 | `getLiveCapabilities` | ✓ | 🟡 | handler在 | |
| 启动直播(对 App) | `startLiveStream` | ✗ | 🟡 | 无 handler;App 直播走 TUTK(待接) | |
| 截图 | `snapshotChannel` | ✗ | 🟡 | 无 handler;YUV 抓拍待接平台 | |
| 显示输出 HDMI4K/LCD/CVBS | — | — | 🟡 | HDMI 已接;LCD/CVBS 时序待真机 | |

## 1.5 GUI 回放 / 检索
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 事件日历(蓝点/月视图) | `X_NightOwl_queryEventCalendar` | ✓ | 🟡 | handler在;接 rsdk 索引+事件 | |
| 事件列表检索 | `X_NightOwl_queryEventList` | ✓ | 🟡 | handler在;接 rsdk_index/event hub | |
| 录像覆盖区间 | `X_NightOwl_queryRecordingInterval` | ✓ | 🟡 | 接 rsdk | |
| 回放能力 | `getPlaybackCapabilities` | ✓ | 🟡 | handler在 | |
| 开始回放 | `startPlayback` | ✗ | 🟡 | 无 handler;rsdk_group_play 引擎✅,会话接线待补 | |
| 事件下载(MP4) | `X_NightOwl_startEventDownload`/`getEventDownloadProgress` | ✓ | 🟡 | handler在;接 rsdk_backup_export | |
| 回放引擎(检索/跨盘/导出) | — | — | ✅ | rsdk_group_query/play/backup | |

## 1.6 录像调度 / 触发
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 连续录像 | — | — | ✅ | streaming record=1→rsdk | |
| 事件录像(motion/human/face/vehicle/doorbell) | — | — | ✅ | `record_sched`+rsdk rectype | |
| 通道录像开关 查询/设置 | `X_NightOwl_get/setChannelRecordingSwitch` | ✓ | 🟡 | 接设置库+record_sched | |
| 录像触发类型 查询/设置 | `X_NightOwl_get/setChannelRecordingTriggers` | ✓ | 🟡 | 接设置库 rec_triggers | |
| 录像计划规则 查询/设置 | `X_NightOwl_get/setRecordingScheduleRules` | ✗ | 🟡 | **无 handler**;设置库 rec_schedule 表在,待实现 | |
| 满盘策略(overwrite/stop) | — | — | ✅ | rsdk+record_sched | |

## 1.7 鉴权 / 账户（GUI + Admin_NOP）
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| NOP 账户登录(Cognito) | `X_NightOwl_loginUser` | ✓ | 🟡 | handler在;离线缓存/Cognito 接线 | |
| 本地解锁 | `X_NightOwl_unlock` | ✓ | 🟡 | 接设置库 auth | |
| 恢复出厂 | `X_NightOwl_resetToFactorySettings` | ✓ | 🟡 | 接清库/复位 | |
| owner 查询/设置 | `X_NightOwl_getOwner`/`setOwner` | ✓ | 🟡 | 设置库 nop_owner 表在 | |
| Admin 锁定(3错1h)/超时 | — | — | 🟡 | auth 表有 lockout;逻辑待接 | |
| 更新 P2P 凭据 | `X_NightOwl_updateP2PCredential` | ✓ | 🟡 | 接设置库 | |
| 设备名 | `setName` | ✓ | 🟡 | 接设置库 system.device_name | |
| 时区 | `X_NightOwl_setTimezone` | ✓ | 🟡 | 接设置库 | |

## 1.8 pushNotification 推送（NVR 侧）
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| 事件→推送脊柱 | — | — | 🟡 | 事件中枢 publish→nop push 引擎(结构在) | |
| 通道推送开关 查询/设置 | `X_NightOwl_get/setChannelsPushNotificationSwitch` | ✓ | 🟡 | 接线 | |
| 推送触发类型 | `X_NightOwl_get/setChannelPushNotificationTriggers` | ✓ | 🟡 | 接线 | |
| 勿扰/打盹/带图 | `setSnooze`/`getSnooze`/`setPushPhotoSwitch` | ✓ | 🟡 | 接线 | |
| 推送到手机(外部 HTTPS) | — | — | ❌ | push server 落地未实现 | |

## 1.9 OTA / Wizard / 系统
| 功能项 | NOP 命令 | handler | 我判断 | 说明 | 你判断 |
|---|---|---|---|---|---|
| NVR 自升级 | `upgradeFirmware`/`checkFirmwareUpgradeStatus` | ✗ | ❌ | 无 handler;升级流程未实现 | |
| 通道(IPC)固件下推 | `X_NightOwl_upgradeChannelFirmware`/`checkChannelUpgradeStatus` | ✓ | 🟡 | handler在;下推流程未接 | |
| Wizard 本地向导 | `setName`/`set_datetime`/`resetToFactorySettings` | ✓ | 🟡 | handler在;向导 UI 由界面驱动 | |
| 设备重启 | `reboot` | ✗ | ❌ | 无 handler | |
| 周维护自动重启(0-6AM)+固件检查 | — | — | 🟡 | 设置库 AutoReboot 在;定时逻辑待接 | |
| 设备信息 | `getDeviceInfo`/`X_NightOwl_getDeviceCapabilities` | ✓ | 🟡 | handler在;聚合真实能力接线 | |
| 网络/WiFi | `getCurrentWifi`/`queryWifiList`/`setWifi`/`getWanInterface` | ✓ | 🟡 | handler在(WiFi 机型);接线 | |

## 1.10 BaseStation 无线基站（本机=有线 PoE NVR，多不适用）
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 无线相机配对 | `startAddWirelessCameras`/`stop...`/`getAddWirelessCamerasStatus` | ✗/✗/✓ | ❌ 不适用 | |
| 无线相机移除 | `startRemoveWirelessCameras` | ✗ | ❌ 不适用 | |
| 显示模式 | `getDeviceDisplayMode`/`setDeviceDisplayMode` | ✓ | 🟡 | |
| USB 备份 | `startRecordingBackup`/`getRecordingBackupProgress` | ✗/✓ | ❌ | |

---

# Part 2 — 相机侧功能（NVR 透传 / NOP→ONVIF 翻译）
> 机制：NOP↔ONVIF 映射 9 域✅ + 逐通道 backend 打标✅。「⤳」= 机制就位，真正生效取决于路由接线与相机硬件。

## 2.1 PTZ 云台
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 方向步进/停止 | `ptzMoveByStep`(+ptzMove/ptzStop/GotoPreset) | ✓ | ⤳ 映射✅ | |
| 人形/移动追踪 | `setPtzTrack`/`getPtzTrack` | ✓ | ⤳ | |
| 预置位 | `getPtzPresets` | ✓ | ⤳ | |
| 全景图 | `takePanoramicView`/`queryPanoramicStatus` | ✗/✓ | 🟡 take 无 handler | |

## 2.2 视频 / 直播参数
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 分辨率 查询/设置 | `X_NightOwl_get/setChannelCurrentVideoResolution`/`getChannelSupportedVideoResolutions` | ✓ | ⤳ | |
| 亮度/对比度/方向 | `X_NightOwl_get/setVideoBrightness`/`Contrast`/`Orientation` | ✓ | ⤳ | |
| 媒体档 | `getProfile` | ✓ | ⤳ 映射(Media2) | |
| 截图 | `snapshotChannel` | ✗ | 🟡 | |

## 2.3 音频 / 对讲
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 对讲能力 | `getSpeakerCapabilities` | ✓ | ⤳ | |
| 启动对讲 | `startSpeaker` | ✗ | ❌ 无 handler;对讲路径(7000/WebSocket)未接 | |

## 2.4 白灯 SpotLight
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 手动开关 | `X_NightOwl_set/getChannelLightSwitch` | ✓ | ⤳ | |
| 自动点灯开关/模式/时长 | `X_NightOwl_...ChannelLightDetectionSwitch`/`LightMode`/`LightDuration`/`LightAutoSwitch` | ✓ | ⤳ | |

## 2.5 泛光灯 FloodLight
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 手动开关/计划/PIR/告警联动/时长/亮度/距离 | `set/getChannelFloodLightSwitch`/`Schedule`/`PirSwitch`/`DetectSwitch`/`Duration`/`setFloodLightIntensity`/`Distance` | ✓ | ⤳ | |

## 2.6 警笛 AudioAlert
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 事件联动/声源音量/手动/一键紧急 | `X_NightOwl_...ChannelAudioAlert*`/`setPanicSwitch` | ✓ | ⤳ | |
| 指示灯 | `set/getChannelIndicatorLightSwitch` | ✓ | ⤳ | |

## 2.7 检测 / AI
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 传感器配置(human/face/vehicle/animal/package) | `get/setChannelSensorConfig` | ✓ | ⤳ 映射(§8 Object) | |
| 活动区域 | `X_NightOwl_getChannelActivityZoneTypes`/`getChannelTriggerActivityZone` | ✓ | ⤳ 映射(§9 Line/Field) | |
| 移动灵敏度 | `X_NightOwl_get/setChannelMotionSensitivity` | ✗/✓? | ⤳ | |
| 事件上报(8012 事件中心) | (eMSG_CMD_*) | — | 🟡 | nop `svc_event8012_client` 在;app 逐相机 attach 未接 | |

## 2.8 相机 OTA / 录像触发（相机侧）
| 功能项 | NOP 命令 | handler | 我判断 | 你判断 |
|---|---|---|---|---|
| 通道设备升级 | `X_NightOwl_upgradeChannelFirmware` | ✓ | 🟡 | |
| 相机录像触发类型 | `X_NightOwl_get/setChannelRecordingTriggers` | ✓ | ⤳/🟡 | |

---

# Part 3 — 核心机制
| 项 | 我判断 | 说明 | 你判断 |
|---|---|---|---|
| NOP↔ONVIF 映射 9 域 | ✅ | nop_sdk 映射层完成,双 build 通过 | |
| 三档路由(非透传/透传/翻译) | 🟡 | 结构在;活文档 NOP_NONPASSTHROUGH_APIS.md 边补 | |
| 密码算法(P_enh/P_8012/P_act/激活AES256) | ✅ | crypto 实现,对上文档向量;**KEY_X/Y+AES 盐待你填** | |
| 8089 NOP 服务端(界面 /APPJsonCmd) | ✅ | nvr_app 已起 | |
| 界面看护/崩溃重启 + mnt 部署 | ✅ | deploy/nvr_supervisor.sh | |
| 运行期日志(串口) | ✅ | nvr_log.h | |

---
## 统计（我的判断，约数）
- **✅ 已接真实**：~22（录像/回放引擎/预览/拉流/分类/云存上传/rsdk_cloud/事件中枢/配置库/8089/看护/日志/密码算法）
- **⤳ 透传/翻译相机（机制✅，待路由接线）**：~20（PTZ/灯/警笛/检测/视频参数 等相机能力，handler 基本都在）
- **🟡 handler在但未接真实子系统**：~40（存储/云存开关/账户/录像触发/推送/回放会话/LAN Add/激活握手/设备信息 等——多数是「cap handler→真实子系统/设置库」接线）
- **❌ 无handler/未实现**：~10（reboot/upgradeFirmware/startPlayback/startLiveStream/startSpeaker/无线BaseStation/同步云存/推送落地/录像计划规则）

> 结论：**命令面(handler)覆盖很广（293 个）**，主要工作量在把 handler **接到真实子系统/设置库**（🟡→✅）与**路由到相机**（⤳ 落地）。你逐项在「你判断」列标：确认✅ / 提优先级 / 本期做或不做。
