# NVR 本机功能清单(含实现文件)

> 生成:2026-08-06 · 范围:**NVR 自己做的功能**(不含透传给相机、ONVIF 映射、nop SDK 对外 API)
> 权威来源:本地命令表 `g_nvr_cmd_table[]`([nvr_cmd_table.c](../app/router/nvr_cmd_table.c))+ 后台常驻服务
>
> **状态**:✅ 已实现 · 🟡 半成品/桩/依赖链未接 · ⛔ 空缺
> 目标:先把 NVR 本机功能补完整,再动相机侧(ONVIF)。

---

## 一、命令型功能(GUI/APP 下发 → NVR 本地处理)

### 1. 出图 / 显示 —— 实现:[nvr_cmd_display.c](../app/router/nvr_cmd_display.c)
底层:`nvr_preview.c`、`nvr_chan_persist.c`(映射/分辨率)、`nvr_channel.c`(通道状态)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `GUI_setDeviceDisplayMode` / `GUI_getDeviceDisplayMode` | 分屏模式(1/4/8/9…)读写 | `cmd_GUI_set/getDeviceDisplayMode` | ✅ |
| `GUI_setChannelMapping` / `GUI_getChannelMapping` | 画面-通道映射读写 | `cmd_GUI_set/getChannelMapping` | ✅ |
| `GUI_setDeviceDisplayExt` / `GUI_getDeviceDisplayExt` | 显示扩展参数 | `cmd_GUI_set/getDeviceDisplayExt` | ✅ |
| `GUI_getSysDisplay` / `GUI_setSysDisplay` | 系统显示(分辨率/输出) | `cmd_GUI_get/setSysDisplay` | ✅ |
| `X_NightOwl_getChannelStatus` | 各通道在线/信号状态 | `cmd_X_NightOwl_getChannelStatus` | ✅ |
| `GUI_longPolling` | 状态变更长轮询推送 | `cmd_GUI_longPolling` | ✅ |
| `X_NightOwl_getDeviceCapabilities` | 设备/各通道能力集(读 DB caps) | `cmd_X_NightOwl_getDeviceCapabilities` | ✅ |
| `X_NightOwl_setChannelZoomPan` / `getChannelZoomPan` | 单通道电子放大/平移 | `cmd_X_NightOwl_set/getChannelZoomPan` | 🟡 **回显桩,不实际裁剪**(display.c:145) |

> 🟡 关联:电子放大链未接(`nvr_preview_single_zoom` 死);按通道分辨率持久化未接(`nvr_chan_persist_set/get_res` 死,display 输出硬编码)。

### 2. LAN 子设备接入 —— 实现:[nvr_cmd_lan.c](../app/router/nvr_cmd_lan.c)
底层:`nvr_channel.c`(增删/列表)、`nvr_onvif`(发现/取流)、`nvr_dev_classify`(nop/onvif 分类)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `GUI_LanSearch` | 局域网发现未绑定设备 | `cmd_GUI_LanSearch` | ✅ |
| `GUI_GetAddedLanDevices` | 已绑定清单 | `cmd_GUI_GetAddedLanDevices` | ✅ |
| `GUI_getLanDevice` / `GUI_setLanDevice` | 单设备详情读/写 | `cmd_GUI_get/setLanDevice` | ✅ |
| `GUI_LanAddDevice` | 添加子设备(nop 空密码/onvif 激活密码/PoE 口映射) | `cmd_GUI_LanAddDevice` | ✅ |
| `GUI_LanDelDevice` | 删除子设备 | `cmd_GUI_LanDelDevice` | ✅ |

### 3. 系统 / 账户 —— 实现:[nvr_cmd_system.c](../app/router/nvr_cmd_system.c)
底层:`nvr_settings.c`(KV/owner)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `setName` / `getName` | 设备名读写 | `cmd_set/getName` | ✅ |
| `getDeviceInfo` | 设备信息(SN/型号/版本) | `cmd_getDeviceInfo` | ✅ |
| `X_NightOwl_setTimezone` | 时区设置 | `cmd_X_NightOwl_setTimezone` | ✅ |
| `reboot` | 重启 | `cmd_reboot` | ✅ |
| `X_NightOwl_setOwner` / `getOwner` | 绑定账户读写 | `cmd_X_NightOwl_set/getOwner` | ✅ |
| `GUI_getRemoteAccessState` / `setRemoteAccessState` | 远程访问(BLE+P2P)门控 | `cmd_GUI_get/setRemoteAccessState` | ✅ |

### 4. 云存配置 —— 实现:[nvr_cmd_cloud.c](../app/router/nvr_cmd_cloud.c)
底层:`nvr_settings.c`(`cloud.switch` KV + `cloud_channel` 表)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `X_NightOwl_setCloudRecordSwitch` / `getCloudRecordSwitch` | 云存总开关 | `cmd_X_NightOwl_set/getCloudRecordSwitch` | ✅ |
| `setCloudRecordConfigs` / `getCloudRecordConfigs` | 每通道云存配置(触发/码流) | `cmd_set/getCloudRecordConfigs` | ✅ |

> 说明:此处只存**配置**;真正上传由后台「云上传链路」执行(见二.3)。

### 5. 录像 / 推送配置 —— 实现:[nvr_cmd_record.c](../app/router/nvr_cmd_record.c)
底层:`nvr_settings.c`(record/push 配置)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `X_NightOwl_set/getChannelRecordingTriggers` | 每通道录像触发(移动/常录…) | `cmd_..._ChannelRecordingTriggers` | ✅ |
| `X_NightOwl_set/getChannelRecordingSwitch` | 每通道录像开关 | `cmd_..._ChannelRecordingSwitch` | ✅ |
| `X_NightOwl_set/getChannelsPushNotificationSwitch` | 每通道推送开关 | `cmd_..._ChannelsPushNotificationSwitch` | ✅ |

### 6. 存储管理 —— 实现:[nvr_cmd_storage.c](../app/router/nvr_cmd_storage.c)
底层:`storage_mgr.c`、`storage_disk.c`

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `getStorageInfo` / `X_NightOwl_getStorageInfo` | 存储容量/使用 | `cmd_getStorageInfo` | ✅ |
| `formatStorage` | 格式化 | `cmd_formatStorage` | ✅ |
| `getAllDisksHealth` | 各盘健康 | `cmd_getAllDisksHealth` | ✅ |
| `getCurrentStorage` / `setCurrentStorage` | 当前存储盘读写 | `cmd_get/setCurrentStorage` | ✅ |

### 7. OTA 升级 —— 实现:[nvr_cmd_ota.c](../app/router/nvr_cmd_ota.c)
底层:`nvr_ota.c`

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `upgradeFirmware` | 触发固件升级 | `cmd_upgradeFirmware` | ✅ |
| `checkFirmwareUpgradeStatus` | 升级进度查询 | `cmd_checkFirmwareUpgradeStatus` | ✅ |

### 8. 事件查询 —— 实现:[nvr_cmd_event.c](../app/router/nvr_cmd_event.c)
底层:`rsdk` 录像索引(`rsdk_group_query`)

| 命令 | 功能 | handler | 状态 |
|---|---|---|---|
| `X_NightOwl_queryEventList` | 时间段事件列表(接录像索引) | `cmd_X_NightOwl_queryEventList` | ✅ |
| `X_NightOwl_queryEventCalendar` | 事件月历 | `cmd_X_NightOwl_queryEventCalendar` | 🟡 **待细化**(event.c:2 注) |

---

## 二、后台常驻服务(非命令触发)

| 功能 | 实现文件 | 说明 | 状态 |
|---|---|---|---|
| **命令路由/分派** | [nvr_cmd_router.c](../app/router/nvr_cmd_router.c)、[nvr_cmd_table.c](../app/router/nvr_cmd_table.c) | 三入口 → 本地表/透传/映射 | ✅(自建 HTTP 服务块是死码) |
| **8089 入口** | `components/nop`(`nop_http_server`) | GUI inbound 唯一入口 | ✅ |
| **P2P 远程(APP 入口)** | [nvr_tutk.c](../components/cloud_tutk/src/nvr_tutk.c) | TUTK 会话 init/start/stop | 🟡 **视频帧下发未接**(`nvr_tutk_send_video` 无人调) |
| **BLE 配网** | `app/ble/nvr_ble.c` | 配网通路,复用命令路由 | 🟡 板级 GATT 注册 TODO(nvr_app.c:225) |
| **通道管理/发现** | [nvr_channel.c](../app/channel/nvr_channel.c)、[nvr_chan_persist.c](../app/config/nvr_chan_persist.c) | 加载/绑定/tick 巡检/LAN 复发现/取流 | ✅ |
| **录像写入** | `components/recorder`(`rsdk_rec.c`、`rsdk_meta.c`)、`components/streaming`(`stream_router.c`) | 取流→分段落盘→SQLite 索引 | ✅ |
| **存储守护** | `storage_mgr.c`、`storage_disk.c`、`storage_guard.c` | 扫盘/装配/健康/tick | 🟡 **裸盘守护 `guard_check` 未周期调度** |
| **云上传(VSaaS)** | `uploader.c`、`ts_mux.c`、`http_vsaas.c` | 录像→TS 切片→HTTP 上云 | ✅ |
| **事件中枢** | [nvr_event.c](../app/event/nvr_event.c) | init/tick/deinit 已接 | 🟡 **无检测源喂 `nvr_evt_ingest`**,事件驱动录像不可达 |
| **NTP 校时** | [nvr_netime.c](../app/netime/nvr_netime.c) | 周期校时,维护 `synced` 态 | ✅ |
| **网络配置(eth0/eth1/PoE/UPnP)** | [nvr_netime.c](../app/netime/nvr_netime.c)、[nvr_cmd_network.c](../app/router/nvr_cmd_network.c) | Linux 读 + BusyBox 写；LOCAL 命令见 §A7 | ✅(NetPort 热切换、SMTP 465 待做) |
| **OTA 引擎** | [nvr_ota.c](../app/ota/nvr_ota.c) | 下载/写入/进度 | ✅ |
| **设备激活/加密** | [nvr_crypto.c](../components/crypto/src/nvr_crypto.c) | 子设备激活密码 | 🟡 走**明文**激活;AES/增强为空 key 桩 |
| **MP4 备份导出** | `rsdk_backup.c` | 录像导出 MP4 | 🟡 muxer 无真实调用(仅 examples) |
| **应用主循环/门控** | [nvr_app.c](../app/src/nvr_app.c) | 装配各模块、账户门控启停 BLE+P2P | ✅ |

---

## 三、数据库(SQLite)

NVR 用两个 SQLite 库。建库 DDL 均为 `CREATE TABLE IF NOT EXISTS`(不重建、不动老数据)。

### 库 A · 设置库 `nvr_settings.db`
路径:`<config_dir>/nvr_settings.db`([nvr_app.c:296](../app/src/nvr_app.c#L296)) · WAL · `chmod 600` · 实现:[nvr_settings.c](../components/config/src/nvr_settings.c)

| 表 | 关键字段 | 用途 | 对应功能 / 命令 | 状态 |
|---|---|---|---|---|
| `setting` | key, ival, sval, bval, updated | 通用 KV(如 `cloud.switch`、`system.sn`、时区) | system / cloud 开关等 | ✅ |
| `meta_kv` | key, val | 通用文本 KV | 杂项标志 | ✅ |
| `auth` | id=1, pw_algo, pw_hash, pw_salt, fail_count, lockout_until | 本地管理员口令 | 登录鉴权 | 🟡 **无改密码写入流程** |
| `nop_owner` | id=1, owner_id, username, stoken, updated | NOP 绑定账户 | `setOwner`/`getOwner`、远程门控 | ✅ |
| `camera` | chn(PK), name, protocol, kind, backend, ip, mac, user, password, onvif_port, url, poe_port, serial, model, bound, active … | 通道=子设备主表 | LAN 接入全套 | ✅ |
| `camera_capability` | chn(PK), caps_json, signal, probed_at | 每通道能力集(上线探测写) | `getDeviceCapabilities` | ✅ |
| `record_config` | chn(PK), record_on, triggers, stream_type | 每通道录像配置 | `set/getChannelRecording*` | ✅ |
| `push_config` | chn(PK), switch_on, dnd_enable, dnd_start/end, dnd_weekdays, time_unit | 每通道推送+免打扰 | `set/getChannelsPushNotificationSwitch` | ✅ |
| `cloud_channel` | chn(PK), stream_type, triggers, enable | 每通道云存配置 | `set/getCloudRecordConfigs` | ✅ |
| `schedule` | chn, domain, sensor, rule_id, weekdays, start/end_hms | 排程规则(连续/事件+云存) | — | 🟡 **有表无命令**(排程未接) |
| `local_link` | id=1, network_type, mac, ip, mask, gateway, dns1/2 | eth0 网络配置 | `GUI_get/setLocalLink` | ✅ |
| `email_alert` | id=1, enable, receiver1-5, smtp_*, use_ssl, interval | 邮件告警 | `GUI_get/setEmailAlert`、`GUI_testEmailAlert` | ✅(SMTP 465 待做) |
| `ftp` | id=1, enable, server, port, user, password, remote_dir | FTP 上传 | `GUI_get/setFTP` | ✅ |
| `ddns` | idx(PK), domain, enable, hostname, ddns_key, user, password | DDNS | `GUI_get/setDDNS` | ✅ |

### 库 B · 录像元数据索引库 `meta.db`
路径:`<config_dir>/meta.db`([nvr_app.c:325](../app/src/nvr_app.c#L325);默认 `/config/meta.db`) · WAL · 实现:[rsdk_meta.c](../components/recorder/src/rsdk_meta.c) · DDL:[sql/meta_schema.sql](../components/recorder/sql/meta_schema.sql)

| 表 | 关键字段 | 用途 | 对应功能 | 状态 |
|---|---|---|---|---|
| `meta_doc` | id(PK), ts, ts_ms, chn, event_id, doc_type, seg_disk/chunk/off/pts(视频绑定), storage, enc, json_len, json, meta_disk/off/len | 事件/录像元数据文档索引(json 列存完整结构,其余列为检索键) | `queryEventList`(`rsdk_group_query`)、AI 检索 | ✅(写入活;AI 检索索引就绪) |

索引:`ix_meta_ts`(时间)、`ix_meta_ce`(chn,event)、`ix_meta_et`(event)、`ix_meta_dt`(类型,时间);
表达式索引(仅 storage=0 且明文):`cls`/`color`/`plate`/`face_id`/`event`(智能检索);FTS5 全文可选(schema 注释里,默认关)。

---

## 四、待补完的 NVR 本机功能(🟡 汇总,建议优先级)

| # | 功能 | 缺口 | 涉及文件 |
|---|---|---|---|
| 1 | **事件驱动录像** | 没有检测源(移动/AI)回调调用 `nvr_evt_ingest` | nvr_event.c、nvr_channel.c、streaming |
| 2 | **APP 实时/回放视频下发** | P2P 帧出口 `nvr_tutk_send_video` 没接取流 | nvr_tutk.c、stream_router.c |
| 3 | **电子放大(ZoomPan)** | handler 是回显桩,不驱动裁剪;`nvr_preview_single_zoom` 死 | nvr_cmd_display.c、nvr_preview.c |
| 4 | **按通道分辨率持久化** | display 输出硬编码;`chan_persist_set/get_res` 死 | nvr_cmd_display.c、nvr_chan_persist.c |
| 5 | **事件月历** | queryEventCalendar 待细化 | nvr_cmd_event.c |
| 6 | **裸盘防挂载守护** | `nvr_storage_guard_check` 未周期调度 | storage_guard.c、nvr_app.c |
| 7 | **改密码/账户写入** | `nvr_settings_auth_set` 无写入流程 | nvr_settings.c、nvr_cmd_system.c |
| 8 | **AES 激活口令**(可选) | 空 key 桩,现走明文 | nvr_crypto.c、nvr_cmd_lan.c |
| 9 | **MP4 备份导出**(可选) | muxer 未接命令/UI | rsdk_backup.c |
| 10 | **排程录像/云存** | `schedule` 表有 API 无命令 | nvr_settings.c、record_sched |
| 11 | **NetPort 热切换** | `setNetPort` 仅落库，8089/RTSP 监听未随改口重启 | nvr_app、nop_http_server |
| 12 | **SMTP SSL/465** | `GUI_testEmailAlert` 仅 25/587 明文 | nvr_netime.c + OpenSSL |

---

> 配套文档:[死代码清理清单.md](死代码清理清单.md)(按数据链路的删/留/待实现明细)。
> 下一步:你在本表「三」里圈定要先做哪几项,我逐项接线实现。
