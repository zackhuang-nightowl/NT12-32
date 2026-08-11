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

状态图例：
- `✔` — LOCAL 表已登记 **且** NVR 真实实现
- `待做` — LOCAL 表已登记，当前仅 **cap 回落/空桩**（见 `nvr_cmd_table.c` 行尾 `/* 待做:... */` 注释）
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
| `getIotcAuthKey` / `GUI_getUID` | 待做 | TUTK 配置 | cap 回落；**TUTK UID/AuthKey 对接待实现** |
| `GUI_getAutoRebootSetting` / `GUI_setAutoRebootSetting` | ✔ | 设置库 KV | 周维护 |
| `GUI_getSystemLog` / `getLog` | 待做 | NVR 日志 | cap 空页 |

### A2. 账户 / 鉴权（本地 Admin + NOP owner）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_login` / `GUI_logout` / `GUI_LoginPage` / `GUI_getLoginStatus` | 待做 | 设置库 `auth` | cap 内存桩 |
| `GUI_createUser` / `GUI_deleteUser` / `GUI_getUsers` / `GUI_getUserGroupPermissions` | 待做 | 设置库 | cap 内存桩 |
| `GUI_forgetPassword` | 待做 | 设置库 | cap 内存桩 |
| `X_NightOwl_setOwner` / `X_NightOwl_getOwner` | ✔ | 设置库 `nop_owner`(stoken) | 云存凭据，非易失 |

### A3. LAN Add / 发现 / 即插即用（见 BIND_IPC_FLOW）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_LanSearch` | ✔ | 发现(WS-Disc/34569) | 5s 回复 |
| `GUI_LanAddDevice` | ✔ | 通道绑定 | 60s 超时 |
| `GUI_LanDelDevice` | ✔ | 通道表 | 仅本地删 |
| `GUI_GetAddedLanDevices` | ✔ | 通道表 + 状态 | |
| `GUI_getLanDevice` / `GUI_setLanDevice` | ✔ | 通道/增强配置 | config 弹窗 |
| `getEnhancedSecurity` / `setEnhancedSecurity` | 待做 | NVR 编排(对相机代执行) | 密码算法 §crypto |
| `X_NightOwl_getDeviceActive` / `X_NightOwl_setDeviceActive` | 待做 | NVR 编排 + 设置库存密码 | AES256 激活 |
| `GUI_getChannelMapping` / `GUI_setChannelMapping` | ✔ | 通道表 | |
| `getChannelsStatus` | ✔ | 通道状态机 | NVR 聚合 |
| `getChannelStats` / `getChannelLoading` | 待做 | 通道/流统计 | cap 回落 |

### A4. 存储
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_getStorageInfo` / `getStorageInfo` | ✔ | `components/storage` | |
| `GUI_getHddConfig` / `GUI_setHddConfig` | ✔ | 设置库 `storage.hdd_full` | |
| `formatStorage` | ✔ | storage format | |
| `getCurrentStorage` / `setCurrentStorage` | ✔ | storage | |
| `getAllDisksHealth` | ✔ | storage health(SMART) | |

### A5. 云存（NVR 本地，见 cap_cloud）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `X_NightOwl_setCloudRecordSwitch` / `getCloudRecordSwitch` | ✔ | 设置库 `cloud.switch` | 门控上传器 |
| `getCloudRecordConfigs` / `setCloudRecordConfigs` | ✔ | 设置库 `cloud_channel` | mode 只读 |
| `getChannelCloudRecordStats*` / `getCloudStatusHistory` / `getCurrentClouds` | 待做 | rsdk_cloud / TUTK 状态 | cap 回落 |

### A6. 录像 / 回放（来自 NVR 盘）
| 命令 | 状态 | 来源 | 备注 |
|---|---|---|---|
| `GUI_getChannelEventRecordingSchedule` / `GUI_setChannelEventRecordingSchedule` | 待做 | 设置库 `schedule` | cap 回落 |
| `getChannelRecordingTime` / `getChannelRecordingContent` | 待做 | recorder 索引 | cap 回落 |
| `GUI_getPlaybackMode` / `GUI_playbackControl` | ✔ | nvr_playback(+Notify/倍速/倒放) | |
| `GUI_get/setPlaybackAudio` | ✔ | 宫格 enable[] 多路按接口 | |
| `GUI_ChannelBackupFiles` / `GetChannelBackupStatus` / `StopChannelBackup` | ✔ | rsdk_backup_export → USB | |
| `GUI_getFileList` | 待做 | System 文件列表(非回放) | cap 回落 |
| 事件/日历查询（queryEventList/Calendar 等） | ✔ | meta DOC_CLOUD + EVENT 标记 | NVR 汇总 |

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
| `GUI_getRemoteAccessState` / `GUI_setRemoteAccessState` | 待做 | TUTK P2P | **RemoteAccess↔TUTK 联动待实现** |
| `GUI_getDeviceDisplayMode` / `GUI_setDeviceDisplayMode` / `GUI_getSysDisplay` | ✔ | preview/vout | |
| `GUI_longPolling` | ✔ | 事件/状态推送 | 长轮询 |
| `getReportServer` / `getEnvironment` | 待做 | 配置 | cap 回落 |

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
- 2026-08-11：补全 §A LOCAL 路由表；cap 接线 `nvr_cmd_nop_dispatch`；待做项用表内注释标注。
- 2026-08-11：**§A7 网络/时间整机命令** — `nvr_netime.c` 读 Linux 本地系统、写 BusyBox（对齐 na51090 SDK `S10_Net`/`default.script`）；`nvr_cmd_network.c` 接 LOCAL handler；UPnP/PoE/SMTP 测试由 cap 回落改为真实实现。详见 [修改日志.md](修改日志.md)「2026-08-11 · 网络 LOCAL 实现」。
- 2026-08-12：**WAN/LAN 带宽** — `GUI_getWanInterface`/`getWanInterface` 读 `/sys/class/net` 实时链路；`GUI_getLanInterface` 读 eth0 `speed` 返回 `totalPhysicalBandwidth`/`maxRxBandwidth`；其余网络子项（allocated 带宽、DDNS 客户端、FTP 服务、Email SSL/自动发信、UPnP 映射、PoE PowerUsed、RemoteAccess↔TUTK 等）登记为**待实现**。
