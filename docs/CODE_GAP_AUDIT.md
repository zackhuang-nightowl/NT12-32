# 代码级缺口审计（实码对照）

> 对照分支实码逐项核实：**什么真的没做** vs **200 假数据桩** vs **部分实现** vs **真机待核**。
> 审计日期：2026-08-20。活状态仍以 [STATUS.md](STATUS.md) 为准；本文供开发排期用。

**图例**

| 标记 | 含义 |
|------|------|
| **501** | handler 明确 `nvr_resp_not_support()` 或未注册且 nop 回落失败 |
| **桩** | HTTP 200，但数据来自 `components/nop` cap 内存默认值/全零，**未接 NVR 真实子系统** |
| **仅配置** | GET/SET 落 SQLite，**无后台 daemon/上传/同步** |
| **部分** | 主链路已通，列出的子能力缺失 |
| **真机待核** | 代码已接线，需板级验证 |

---

## 1. 明确 501 / 无业务实现

| 项 | 代码位置 | 说明 |
|----|----------|------|
| `GUI_setNetPort` | `nvr_cmd_network.c` | 产品约定暂不改口；8089/554 启动时读库 |
| `X_NightOwl_startEventDownloadwithURL` | `nvr_cmd_event.c` | 裸盘 dynamic 模式，无现成 URL 可给 |
| `getNotificationSetting` | `nvr_cmd_system.c` | TUTK agent 用；NVR 无内置推送配置 → 501 为设计 |
| 未在 `g_nvr_cmd_table` 且 nop 无 handler | `nvr_cmd_router.c` | 最终 501 |
| `getChannelStats` / `getChannelLoading` / `getCloudStatusHistory` / `getChannelCloudRecordStats*` / `getChannelRecordingContent` / `getLog` / `GUI_getSystemLog` / `getReportServer` / `getEnvironment` | `nvr_cmd_misc.c` / `nvr_cmd_system.c` | 产品不需要 → **501** |
| `startCloudRecordTest` / `stopCloudRecordTest` / `getCloudRecordTestProgress` / `getCloudRecordLogConfig` / `setCloudRecordLogConfig` | `nvr_cmd_cloud.c` | 暂不实现 → **501** |
| Chromecast / Google Home | — | `NVR_URL_SMART_HOME` **零引用** |
| Wizard 四场景状态机 | — | 底层命令分散，无向导编排 |
| 产测 ProductionTest | — | 未开始 |
| 无盘 BaseStation 实时 tee 上传 | streaming 旁路标注 | v2 延后 |
| 无线/电池机配对 | — | 本期不做 |
| LVGL GUI 本体 | 独立仓 | 本仓只保证 8089 协议 |

---

## 2. 原 NOP cap 单机桩（已删除）

2026-08-20 起：`cap_agent.c` / `cap_cloud.c` 改为空注册；`cap_misc_ext.c` 仅保留 ONVIF mapping 入口（OSD/隐私区/sensor config）；`cap_ai_advanced.c` 去掉 event ext-info 内存桩。下列命令若仍出现在 LOCAL 表，直接 **501**，不再 200 假数据：

| 命令 | 原 cap 文件 | 现况 |
|------|-------------|------|
| `getChannelStats` / `getChannelLoading` / `getCloudStatusHistory` / `getChannelCloudRecordStats*` / `getChannelRecordingContent` | cap_misc_ext | LOCAL **501** |
| `getLog` / `GUI_getSystemLog` / `getReportServer` / `getEnvironment` | cap_agent | LOCAL **501** |
| 云存 test/log 五命令 | cap_cloud | LOCAL **501** |
| `agent_diagnosis` | cap_agent | **已删注册** |

**NVR 仍用 cap 的真实路径**：带 `channel` 的 ONVIF 映射（`cap_ai*` / `cap_misc_ext` OSD 等）、以及未进 LOCAL 表时的 `fallback_nop`（无 handler → 501）。

**对比：已接真实子系统的 LOCAL 命令**（同表但非桩）：`getStorageInfo`/`formatStorage`/`getAllDisksHealth`（`nvr_cmd_storage.c`+`rsdk_smart`）、云存开关/配置（`nvr_cmd_cloud.c`+settings+uploader 门控）、推送全套（`nvr_cmd_push.c`+`nvr_push.c`）、`getChannelsStatus`（通道状态机）、`getCurrentClouds`（`system.json`→KV `cloudServer.*`）、`getCableConnectStatus`（DRM 热插拔）等。

---

## 3. 仅配置读写（无后台服务）

| 项 | 代码 | 缺口 |
|----|------|------|
| DDNS | `GUI_get/setDDNS` → SQLite | **无** inadyn/ezip 等客户端 `apply` |
| FTP | `GUI_get/setFTP` → SQLite | **无** 录像/告警 FTP 上传引擎 |
| 邮件告警 | `GUI_get/setEmailAlert` + `nvr_net_email_test` | 25/587 明文 AUTH 可测；**465 SSL 未实现**（`nvr_netime.c:582`） |
| `GUI_getLanInterface` | `nvr_cmd_network.c` | 缺 `allocatedRxBandwidth`（注释：无流统计来源） |

UPnP **有** `nvr_net_upnp_apply`（启停 miniupnpd）；DDNS/FTP 与之不同，仅落库。

---

## 4. 部分实现（主路径有，子能力缺）

### 4.1 推送 `app/push/nvr_push.c`

| 能力 | 状态 |
|------|------|
| 策略（switch/triggers/DND/snooze）+ 读 `rsdk_pic` + 图床 + TPNS | ✅ 已接线（`nvr_app.c` `nvr_push_start`） |
| motion/human/face/vehicle/doorbell | ✅ `payload_key` + `event_type` 完整 |
| animal / package / lineCross / fieldIntrusion | **部分**：`trigger_of`/`push_event_type` 有值，但 `payload_key()` **无 E_DVR_* → 不发 TPNS**（`handle_job` L364-366） |
| 低电/满电推送 | ❌ NVR 无电池机语义 |

### 4.2 抓拍

| 能力 | 状态 |
|------|------|
| 事件抓拍 → `rsdk_pic` | ✅ |
| `snapshotChannel` | ✅ ONVIF GetSnapshot（非本机 YUV 硬解） |
| 本机可见窗 YUV 截帧 | ❌ `mhal_vdec.c` TODO phy→用户态 |

### 4.3 对讲 `app/talk`

| 能力 | 状态 |
|------|------|
| App→隧道→本机 7000→相机 | ✅ |
| HDMI/RCA MIC 采集上送 | ❌ 未接 |

### 4.4 显示 `platform/media_hal`

| 能力 | 状态 |
|------|------|
| HDMI 1080p/4K 阶梯 | ✅ `mhal_vout.c` |
| CVBS / VGA 输出 | ❌ 枚举有，`mhal_vout_init` 仅 HDMI 路径 |
| 预览轮巡定时 | ❌ 无 shuffle/tour 逻辑 |

### 4.5 云存上传 `components/cloud_uploader`

| 能力 | 状态 |
|------|------|
| 事件切片 → TS → VSaaS | ✅ 链路通 |
| 按 `cloud_channel.stream_type` 取主/子轨 | ✅ `rsdk_group_query_stream` + 登记门控 |
| MPEG-TS PCR/连续性 | **部分**：`ts_mux.c` 极简 PES，上云/App 待调 |

### 4.6 RSDK / streaming（v2 已落地后的遗留）

| 项 | 代码 | 说明 |
|----|------|------|
| per-disk record worker | `stream_record_worker.c` 单线程 | 吞吐优化，非正确性 |
| writer open/close 归属 worker | `stream_router.c` puller 仍 `rsdk_rec_open` | 元数据有锁；worker 注释 UAF 风险 |
| `rsdk_rawdev_pread` 短读 | `rsdk_rawdev.c:48` | 越界仍补 0 当成功（设计 Rel4 未改） |
| RTP Time Mapper wall_time | `stream_router.c` | 用 `time(NULL)` 入队时刻，非 RTP delta |

---

## 5. 出站 URL（`nvr_urls.h`）接线情况

| 常量 | 使用处 | 状态 |
|------|--------|------|
| Cognito IdP / GraphQL | `nvr_cognito.c` / `nvr_graphql.c` | ✅ |
| OTA | `nvr_cmd_ota.c` / `nvr_ipc_ota.c` | ✅ |
| VSaaS / CLOUD_REC | `cloud_uploader` | ✅ |
| PUSH + UPLOAD_IMAGE | `nvr_push.c` | ✅ |
| `NVR_URL_COGNITO` hosted UI | — | 未用（设计：走 IdP API） |
| `NVR_URL_SMART_HOME` | — | ❌ 未引用 |

---

## 6. 真机 / 板级待核（代码已有，未上机验收）

- 32 路并录 + ≤16 窗硬解 VPU/DDR 上限
- 回放 HDMI 音频出声（`mhal_aout` 已接路径）
- OTA nandwrite / A/B 切槽
- IPC OTA status=6 + longPolling 联调
- RSDK v2 soak（32 路边录边回放 + 拔盘 N 小时）
- PoE 交换芯片 `GUI_get/setPoE` 功率读数（若板级 ioctl 未接则为假值）

---

## 7. 已确认**非**缺口（文档曾误标）

| 项 | 实码 |
|----|------|
| RSDK v2 P0–P3 | ✅ 见 [rsdk-v2 spec §13](superpowers/specs/2026-08-20-rsdk-v2-reliable-recording-design.md) |
| 推送外发 TPNS | ✅ `nvr_push.c`（非 §14「外发未做」） |
| `getAllDisksHealth` SMART | ✅ `rsdk_smart_read` + 属性表 |
| LAN Add / 34569 / 绑定握手 | ✅ `nvr_cmd_lan.c` / `nvr_lan34569.c` / `nvr_channel.c` |
| `startLiveStream` / `startPlayback` / 对讲 | ✅ `nvr_cmd_p2p.c` + RTSP |
| `snapshotChannel` | ✅ ONVIF 快照 |
| 连续/事件录像排程 | ✅ `nvr_cmd_record.c` + `nvr_record_sched.c` |
| ZoomPan | ✅ `mhal_vout_set_crop` |

---

## 8. 维护说明

- 新增 LOCAL 命令：在 `nvr_cmd_table.c` 加行时区分 **真实 handler** vs **暂不实现 → 501**（`NVR_NOT_IMPL` / `nvr_resp_not_support`）。
- 产品不需要的功能：**不要**在 cap 里补单机假数据；LOCAL 表直接 501。
- [功能对照_nopdoc.md](功能对照_nopdoc.md) 为 2026-08 快照，大量 🟡 已过时；以本文 + STATUS 为准。
