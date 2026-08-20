# NOP 命令路由 — 非透传 API 名单（活文档）

> **NVR 不是所有 NOP 接口都透传。** 本表是 NOP 路由前门的查表依据，**边实现边补充**：
> 每实现/确认一个接口，就在下表登记「命令 / 分类 / NVR 侧数据来源 / 状态 / 备注」。
> 相关：即插即用连接流程见 [BIND_IPC_FLOW.md](BIND_IPC_FLOW.md)。

## 路由三档决策

给定一条 App 发来的 NOP 命令，前门按顺序判定：

1. **非透传（LOCAL）** — 整机/NVR 级命令，由 NVR 自己的 handler 应答，**不发给相机**。
2. **透传（PASSTHRU）** — 面向某相机通道、且该通道 `backend==0`（NOP）→ 原样转发 NOP 给**该路相机** `/APPJsonCmd`。
3. **翻译（TRANSLATE）** — 面向某相机通道、且 `backend!=0`（nopOnvif/onvif）→ 本机 mapping 翻成 ONVIF SOAP，发到**该通道物理设备** `host:port`（`nop_app_dispatch` 只是翻译入口）。
4. **例外：nopOnvif 私有 NOP**（`nightowl_protocol.md` 白灯/警笛/一键报警/激活）— 虽 `backend!=0`，仍 POST 发现口 `/APPJsonCmd`；通用 ONVIF 不走此口。NVR 侧 Panic 仍需 `args.channel` 选机，转发时剥掉。

**默认规则**：不在本表、且带 `channel` 参数 → 按「面向通道 → PASSTHRU/TRANSLATE」；不带 `channel`
且属整机语义 → 倾向 LOCAL。不确定的登记为 `TBD` 并在实现时确认。

状态图例：
- `✔` — LOCAL 表已登记 **且** NVR 真实实现
- `—` — LOCAL 表已登记，**501**（产品不需要 / 暂不实现）
- `?` — 分类待确认(TBD)

---

## A. 非透传 LOCAL（NVR 本地处理）

`GUI_*` 前缀命令基本都是本地 HMI/整机语义 → LOCAL。

### A1. 设备 / 系统 / 能力
| 命令 | 状态 | NVR 侧数据来源 | 备注 |
|---|---|---|---|
| `getDeviceInfo` | ✔ | NVR 身份(SN/MAC/型号/FW) | 工厂区 + 设置库 |
| `X_NightOwl_getDeviceCapabilities` | ✔ | NVR 聚合 device+channels 能力 | 含 `cloudRecording` |
| `getName` / `GUI_getSysDisplay` / `GUI_getFeatureList` | ✔ | 设置库 / 静态列表 | |
| `getIotcAuthKey` / `setIotcAuthKey` / `getAvPassword` / `setAvPassword` / `getIotcUID` / `GUI_getUID` | ✔ | `/User` 身份（`nvr_identity`） | 出厂 AuthKey=`00000000` AvPwd=`888888`；set 写回 tutkdata.json |
| `GUI_getAutoRebootSetting` / `GUI_setAutoRebootSetting` | ✔ | 设置库 KV | 周维护 |
| `GUI_getSystemLog` / `getLog` | — | 501 | 产品不需要 |

### A2. 账户 / 鉴权（本地 Admin + NOP owner）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_login` / `GUI_logout` / `GUI_getLoginStatus` / `GUI_LoginPage` / `GUI_createUser` / `GUI_deleteUser` / `GUI_setUser` / `GUI_getUsers` / `GUI_getUserGroupPermissions` / `GUI_forgetPassword` | ✔ | `local_user` + Cognito + GraphQL | `LoginPage`：GUI 进出登录窗 Area+Action → result OK |
| `GUI_forgetPassword` | ✔ | ResetCode + 可选清 `nop_owner` | 对齐 AdminPWD；aws 联网由 GUI 提示 |
| `X_NightOwl_setOwner` / `X_NightOwl_getOwner` | ✔ | `nop_owner` + `ble.key` | 带 ownerId 时生成 16 位 hex **BLEKey**（下连 AES） |
| `X_NightOwl_updateP2PCredential` | ✔ | `tutk.authkey` / `tutk.av_password` | 乱数 IotcPwd(8) + AvPwd(6 hex) |
| `X_NightOwl_loginUser` | ✔ | `ble.key` / 出厂 admin | BLE 鉴权；已绑定=ownerId+BLEKey |

### A3. LAN Add / 发现 / 即插即用（见 BIND_IPC_FLOW）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_LanSearch` | ✔ | 发现(WS-Disc/34569) | WS-Disc 5s + 34569 1s，同 IP 合并 |
| `GUI_LanAddDevice` | ✔ | 通道绑定 | 立即入库，不阻塞等连接 |
| `GUI_LanDelDevice` | ✔ | 通道表 | 删物理台：同 IP 多源 channel 一并删 |
| `GUI_GetAddedLanDevices` | ✔ | 通道表 + 状态 | |
| `GUI_getLanDevice` / `GUI_setLanDevice` | ✔ | 通道/增强配置 | `videoSources` 决定启用哪路源（不必是源1） |
| `getEnhancedSecurity` / `setEnhancedSecurity` | ✔ LOCAL | NVR 对 NOP 相机代查/代开/关 digest | `GUI_setLanDevice.enhancedSecurity`：true=NVR 写 random 并用 `P_enh`（ONVIF+8012）；false=SET `random=""` 后空凭据（不以 `random_empty_error` 为准）。random 入库至 reset。401+`Random:` 重试；402/501 清本地。相机接口无 `X_NightOwl_` 前缀；`channel` 默认 1，转发 `dev_chn`。连接不自动 SET |
| `X_NightOwl_getDeviceActive` / `X_NightOwl_setDeviceActive` | ✔ LOCAL | NVR 代查/代激活 | nopOnvif：Discovery/GetScopes 认种后 GET→SET→再 GET；成功写 `admin/P_act`。忽略 GUI 下发 password |
| `GUI_getChannelMapping` / `GUI_setChannelMapping` | ✔ | 通道表 | |
| `getChannelsStatus` | ✔ | 通道状态机 | NVR 聚合 |
| `getChannelStats` / `getChannelLoading` | — | 501 | 产品不需要 |

### A4. 存储
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_getStorageInfo` / `getStorageInfo` | ✔ | `components/storage` | |
| `GUI_getHddConfig` / `GUI_setHddConfig` | ✔ | 设置库 `storage.hdd_full` | |
| `formatStorage` | ✔ | storage format | |
| `getCurrentStorage` / `setCurrentStorage` | ✔ | storage | |
| `getAllDisksHealth` | ✔ | storage health(SMART) | |

### A5. 云存（NVR 本地，`nvr_cmd_cloud.c`）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_setCloudRecordSwitch` / `getCloudRecordSwitch` | ✔ | 设置库 `cloud.switch` | 门控上传器 |
| `getCloudRecordConfigs` / `setCloudRecordConfigs` | ✔ | 设置库 `cloud_channel` | mode 只读 |
| `getCurrentClouds` | ✔ | `system.json` → KV | `cloudServer.current` / `cloudServer.available` |
| `getChannelCloudRecordStats*` / `getCloudStatusHistory` | — | 501 | 产品不需要 |
| `startCloudRecordTest` / `stopCloudRecordTest` / `getCloudRecordTestProgress` / `getCloudRecordLogConfig` / `setCloudRecordLogConfig` | — | 501 | 暂不实现 |

### A6. 录像 / 回放（来自 NVR 盘）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_getChannelEventRecordingSchedule` / `GUI_setChannelEventRecordingSchedule` | ✔ | `schedule` domain=`record_event` | 按 sensor 周排程；触发落盘前门控 |
| `getChannelRecordingTime` / `setChannelRecordingTime` | ✔ | `record.ch.N.post_s` | 事件后录秒数(默认 10)；落盘窗口用 |
| 仅事件 + 预录 | ✔ | `record.ch.N.pre_s`(默认 5) + `event_arm` | 关连续排程仍待命；主+子双路预录/写盘至 post |
| `GUI_getPlaybackMode` / `GUI_playbackControl` | ✔ | nvr_playback(+Notify/倍速/倒放) | |
| `GUI_get/setPlaybackAudio` | ✔ | 宫格 enable[] 多路按接口 | |
| `GUI_ChannelBackupFiles` / `GetChannelBackupStatus` / `StopChannelBackup` | ✔ | rsdk_backup_export → USB | |
| `GUI_getFileList` | ✔ | USB/SD 根目录列文件 | `nvr_cmd_playback.c` |
| 事件/日历查询（queryEventList/Calendar 等） | ✔ | meta DOC_CLOUD + EVENT 标记 + 抓拍 URL | `thumbnailUrl` 仅隧道 GET |
| `AI_getEventExtInfo` / `AI_getEventExtInfoBatchByReverseTime` | ✔ | meta `DOC_AI_EVENT` | 套包：后录结束从 NOP 相机取回后入库；App 查 NVR |
| `AI_getEventExtInfoConfig` / `AI_setEventExtInfoConfig` | ✔ | KV `ai.event_ext_info.*` + 代 SET 相机 | 默认 enable；上线代开 NOP 缓存 |
| `getSpeakerCapabilities` / `startSpeaker` / `stopSpeaker` | ✔ | `nvr_talk` | NOP:7000 / ONVIF backchannel |

### A7. 网络 / 时间 / 显示（整机）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_getLocalLink` / `GUI_setLocalLink` | ✔ | Linux 实时 + 设置库 `local_link` + `nvr_net_apply_eth0` | GET 读 getifaddrs/sysfs/proc/resolv；写 BusyBox ifconfig/route/udhcpc |
| `GUI_getLanInterface` | ✔ | `/sys/class/net/eth0/speed` | `totalPhysicalBandwidth`/`maxRxBandwidth` 读 Linux；**`allocatedRxBandwidth` 待实现**（无流统计，响应不含该字段） |
| `GUI_getWanInterface` / `getWanInterface` | ✔ | `/sys/class/net/*/operstate` | 扫描 eth/wlan；`value`/`connected` 按 eth0/wlan 链路 up；NT12-32 有线机 `list=["eth"]`，有 wlan 时加 `"wifi"` |
| `GUI_getNetPort` / `GUI_setNetPort` | ✔ | KV `network.port.*` | 落库；**8089/RTSP 热切换待实现**（改端口需重启） |
| `GUI_getNTP` / `GUI_setNTP` | ✔ | KV `system.ntp` / `system.time_sync` | set 触发 `nvr_time_resync` |
| `GUI_getDDNS` / `GUI_setDDNS` | ✔ | 设置库 `ddns` | **DDNS 客户端待实现**（当前仅配置读写） |
| `GUI_getUPnP` / `GUI_setUPnP` | ✔ | KV `network.upnp.*` + miniupnpd/upnpd | 启停 miniupnpd；**端口映射规则待完善** |
| `GUI_getFTP` / `GUI_setFTP` | ✔ | 设置库 `ftp` | **FTP 上传服务待实现**（当前仅配置读写） |
| `GUI_getEmailAlert` / `GUI_setEmailAlert` | ✔ | 设置库 `email_alert` | |
| `GUI_testEmailAlert` | ✔ | SMTP 明文 AUTH(25/587) | **465/SSL 待实现**；**事件触发自动发信待实现** |
| `GUI_getPoE` / `GUI_setPoE` | ✔ | eth1 VLAN operstate | set 启停 VLAN；**PowerUsed 待实现**（无 PoE MCU/功耗数据源）；**PoE 供电控制待实现** |
| `GUI_getRemoteAccessState` / `GUI_setRemoteAccessState` | ✔ | KV `service.remote_access` + 账户门控 | 启停 TUTK P2PTunnel + BLE |
| `GUI_getDeviceDisplayMode` / `GUI_setDeviceDisplayMode` / `GUI_getSysDisplay` | ✔ | preview/vout | |
| `getCableConnectStatus` | ✔ | DRM `/sys/class/drm/.../status` | `mhal_vout_get_cable_connect`（HDMI/VGA） |
| `GUI_longPolling` | ✔ | 事件/状态推送 | 长轮询 |
| `getReportServer` / `getEnvironment` | — | 501 | 产品不需要 |

---

## B. 透传 PASSTHRU / 翻译 TRANSLATE（面向相机通道）

带 `channel` 的相机侧控制：`kind=NOP` 透传；`kind=onvif` 走 `nop_onvif_map_dispatch`；
`kind=nopOnvif` 默认同样 mapping，**白灯/警笛/Panic/激活** 走发现口 `/APPJsonCmd`（`nvr_chan_noponvif_priv_func`）。

| 命令(族) | 状态 | 备注 |
|---|---|---|
| PTZ：`ptzMove/ptzByStep/ptzStop/GotoPreset` / `getPtzPresets/getPtzPatrols/getPtzTrack` | ~ | §2 PTZ 映射 |
| OSD：`X_NightOwl_getOSD` / `X_NightOwl_setOSD` | ~ | §5 OSD 映射 |
| 隐私区：`X_NightOwl_get/setChannelPrivacyZone` | ~ | §7 Privacy 映射 |
| 灯：`X_NightOwl_get/setChannelLightSwitch` / `LightDetectionSwitch` | ✔ nopOnvif POST | 上线 GET 探测 → `capabilities[]` `light`；通用 ONVIF 501 |
| 音频告警：`get/setChannelAudioAlert(Switch|DetectSwitch)` | ✔ nopOnvif POST | 上线 GET 探测 → `audioAlert`；通用 ONVIF 501 |
| 一键报警：`get/setPanicSwitch` | ✔ nopOnvif POST | 设备侧无 channel；NVR 用 channel 选机后剥掉 |
| AI 传感器：`get/setChannelSensorConfig` / `getChannelSensorLinkage` | ~ | §8/§9 AI 映射 |
| 活动区域：`X_NightOwl_getChannelActivityZoneTypes` / `get/setChannelTriggerActivityZone` | ✔ | NOP 先透传；失败则 CellMotion mapping。Types：GetRules 含 Motion → `triggers:["pixelChange"]`；SET 对已有规则 ModifyRules |
| 媒体档：`GUI_getChannelMediaProfiles`(相机档)/ `getProfile` | ? | 区分 NVR 档 vs 相机档 |

> ⚠️ 注意易混：`getChannel*` 里既有**相机设置**（→PASSTHRU/TRANSLATE，如 SensorConfig/FloodLight/
> LightSwitch），也有 **NVR 聚合**（→LOCAL，如 ChannelsStatus/ChannelMapping/RecordingTime/
> ChannelEventRecordingSchedule）。逐条实现时在本表定档。

---

## 变更记录
- 初始种子：由计划 §B0.1 + 现有 `components/nop/src/business/caps/*` 命令名归类而成。
- 2026-08-11：补全 §A LOCAL 路由表；cap 接线 `nvr_cmd_nop_dispatch`；待做项用表内注释标注。
- 2026-08-11：**§A7 网络/时间整机命令** — `nvr_netime.c` 读 Linux 本地系统、写 BusyBox（对齐 na51090 SDK `S10_Net`/`default.script`）；`nvr_cmd_network.c` 接 LOCAL handler；UPnP/PoE/SMTP 测试由 cap 回落改为真实实现。详见 [修改日志.md](修改日志.md)「2026-08-11 · 网络 LOCAL 实现」。
- 2026-08-12：**WAN/LAN 带宽** — `GUI_getWanInterface`/`getWanInterface` 读 `/sys/class/net` 实时链路；`GUI_getLanInterface` 读 eth0 `speed` 返回 `totalPhysicalBandwidth`/`maxRxBandwidth`；其余网络子项（allocated 带宽、DDNS 客户端、FTP 服务、Email SSL/自动发信、UPnP 映射、PoE PowerUsed、RemoteAccess↔TUTK 等）登记为**待实现**。
- 2026-08-18：**nopOnvif 私有 NOP** — 白灯/警笛/Panic/激活走发现口 `/APPJsonCmd`；上线 GET 探测 `light`/`audioAlert`；`get/setDeviceActive` 移出 LOCAL 表。
- 2026-08-19：**活动区域 SET** — `setChannelTriggerActivityZone` ModifyRules 已有 CellMotion（不删建）。NOP 先透传，失败再 mapping。
- 2026-08-20：**cap 单机桩清理** — `cap_agent`/`cap_cloud` 空注册；`cap_misc_ext` 仅 ONVIF mapping；产品不需要命令 LOCAL **501**（不再 cap 200 假数据）。见 [修改日志.md](修改日志.md)「cap 单机桩 + LOCAL 501」。
